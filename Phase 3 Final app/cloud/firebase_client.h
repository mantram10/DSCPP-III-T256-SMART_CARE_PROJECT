// ============================================================
// firebase_client.h  -  SmartCare Phase 3
// Authenticated HTTPS access to Firebase from C++.
//
// Design choice: SmartCare already talks to PostgreSQL by running
// psql.exe through system(). This client follows the same pattern
// and runs curl.exe (built into Windows 10 1803+ and Windows 11),
// so no extra C++ libraries are needed.
//
//   1. signIn()  -> Firebase Auth REST (email/password) -> ID token
//   2. get/put/remove(path) -> Realtime Database REST: <db>/<path>.json?auth=<token>
//
// The Realtime Database security rules decide what each hospital
// account may read (see firebase/database.rules.json).
//
// Trade-off: for a production system, libcurl or cpr would avoid
// process spawning and keep the token off the command line.
// ============================================================
#ifndef SMARTCARE_FIREBASE_CLIENT_H
#define SMARTCARE_FIREBASE_CLIENT_H

#include "json_mini.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

namespace smartcare {

struct HttpResult
{
    int status = 0;       // HTTP status; 0 means no response (network error)
    std::string body;     // response body
    std::string error;    // curl / local error text
    bool ok() const { return status >= 200 && status < 300; }
};

inline std::string readEnv(const char *name)
{
#ifdef _MSC_VER
    char *buf = nullptr;
    size_t len = 0;
    std::string value;
    if (_dupenv_s(&buf, &len, name) == 0 && buf)
    {
        value = buf;
        free(buf);
    }
    return value;
#else
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string();
#endif
}

class FirebaseClient
{
public:
    // dbUrl:  e.g. https://smartcare-xxxx-default-rtdb.asia-southeast1.firebasedatabase.app
    // apiKey: the project's Web API key (Project settings > General)
    // authUrl: normally https://identitytoolkit.googleapis.com ; can point to the
    //          Firebase Auth Emulator (http://127.0.0.1:9099/identitytoolkit.googleapis.com)
    FirebaseClient(std::string dbUrl, std::string apiKey,
                   std::string authUrl = "https://identitytoolkit.googleapis.com",
                   std::string dbNamespace = "")
        : dbUrl_(stripSlash(dbUrl)), apiKey_(apiKey), authUrl_(stripSlash(authUrl)),
          ns_(dbNamespace)
    {
#ifdef _WIN32
        curlExe_ = "curl.exe";
#else
        curlExe_ = "curl";
#endif
    }

    // Reads SMARTCARE_FIREBASE_DB_URL, SMARTCARE_FIREBASE_API_KEY and the
    // optional SMARTCARE_FIREBASE_AUTH_URL / SMARTCARE_FIREBASE_NS.
    static FirebaseClient fromEnvironment()
    {
        std::string authUrl = readEnv("SMARTCARE_FIREBASE_AUTH_URL");
        if (authUrl.empty())
            authUrl = "https://identitytoolkit.googleapis.com";
        return FirebaseClient(readEnv("SMARTCARE_FIREBASE_DB_URL"),
                              readEnv("SMARTCARE_FIREBASE_API_KEY"), authUrl,
                              readEnv("SMARTCARE_FIREBASE_NS"));
    }

    // Returns an empty string if configuration is usable, otherwise the problem.
    std::string configProblem() const
    {
        if (dbUrl_.empty())
            return "SMARTCARE_FIREBASE_DB_URL is not set.";
        if (!isSafeBaseUrl(dbUrl_))
            return "Database URL must be https:// (or http://127.0.0.1 / localhost for the emulator) "
                   "and contain no spaces or shell characters.";
        if (apiKey_.empty())
            return "SMARTCARE_FIREBASE_API_KEY is not set.";
        if (!isTokenChars(apiKey_))
            return "API key contains unexpected characters.";
        if (!isSafeBaseUrl(authUrl_))
            return "Auth URL is not a safe https:// URL.";
        if (!ns_.empty() && !isTokenChars(ns_))
            return "SMARTCARE_FIREBASE_NS contains unexpected characters.";
        return "";
    }

    bool signIn(const std::string &email, const std::string &password, std::string &message)
    {
        idToken_.clear();
        std::string problem = configProblem();
        if (!problem.empty())
        {
            message = problem;
            return false;
        }

        std::string body = "{\"email\":\"" + jsonEscape(email) + "\",\"password\":\"" +
                           jsonEscape(password) + "\",\"returnSecureToken\":true}";
        HttpResult r = request(authUrl_ + "/v1/accounts:signInWithPassword?key=" + apiKey_, "POST", &body);

        if (r.status == 0)
        {
            message = "Network error contacting Firebase Auth: " + r.error;
            return false;
        }
        try
        {
            JsonValue j = JsonParser::parse(r.body);
            if (!r.ok())
            {
                const JsonValue *err = j.find("error");
                const JsonValue *msg = err ? err->find("message") : nullptr;
                message = "Sign-in failed (HTTP " + std::to_string(r.status) + "): " +
                          (msg && msg->isString() ? msg->text : std::string("unknown error"));
                return false;
            }
            const JsonValue *tok = j.find("idToken");
            if (!tok || !tok->isString() || !isTokenChars(tok->text))
            {
                message = "Sign-in response did not contain a valid ID token.";
                return false;
            }
            idToken_ = tok->text;
            email_ = email;
            decodeClaims();
            message = "Signed in as " + email_ +
                      (hospital_.empty() ? " (WARNING: no 'hospital' claim on this account)"
                                         : " for " + hospital_);
            return true;
        }
        catch (const std::exception &e)
        {
            message = std::string("Could not read sign-in response: ") + e.what();
            return false;
        }
    }

    bool isSignedIn() const
    {
        // Treat the token as expired 60 s early to avoid edge-of-expiry failures.
        return !idToken_.empty() && (expiry_ == 0 || std::time(nullptr) < expiry_ - 60);
    }

    const std::string &hospital() const { return hospital_; }
    const std::string &email() const { return email_; }

    // GET <db>/<path>.json?auth=<token>
    HttpResult get(const std::string &path) { return dbRequest(path, "GET", nullptr); }

    // PUT writes (replaces) the JSON at path. Security rules decide if it is allowed.
    HttpResult put(const std::string &path, const std::string &json) { return dbRequest(path, "PUT", &json); }

    // DELETE removes the node at path. Security rules decide if it is allowed.
    HttpResult remove(const std::string &path) { return dbRequest(path, "DELETE", nullptr); }

private:
    std::string dbUrl_, apiKey_, authUrl_, ns_, curlExe_;
    std::string idToken_, email_, hospital_;
    std::time_t expiry_ = 0;

    static std::string stripSlash(std::string u)
    {
        while (!u.empty() && u.back() == '/')
            u.pop_back();
        return u;
    }

    // Everything placed on the curl command line is checked against a
    // whitelist, because system() passes it through the shell.
    static bool isTokenChars(const std::string &t)
    {
        if (t.empty())
            return false;
        for (char c : t)
            if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.'))
                return false;
        return true;
    }

    static bool isSafePath(const std::string &p)
    {
        if (p.empty() || p.front() == '/' || p.find("..") != std::string::npos)
            return false;
        for (char c : p)
            if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/'))
                return false;
        return true;
    }

    static bool isSafeBaseUrl(const std::string &u)
    {
        bool https = u.rfind("https://", 0) == 0;
        bool local = u.rfind("http://127.0.0.1:", 0) == 0 || u.rfind("http://localhost:", 0) == 0;
        if (!https && !local)
            return false;
        for (char c : u)
            if (!(isalnum(static_cast<unsigned char>(c)) || c == ':' || c == '/' || c == '.' ||
                  c == '-' || c == '_'))
                return false;
        return true;
    }

    static std::string readFile(const std::string &name)
    {
        std::ifstream f(name, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    HttpResult dbRequest(const std::string &path, const char *method, const std::string *body)
    {
        HttpResult r;
        if (!isSignedIn())
        {
            r.error = "Not signed in, or the session expired. Sign in again.";
            return r;
        }
        if (!isSafePath(path))
        {
            r.error = "Refusing unsafe database path: " + path;
            return r;
        }
        std::string url = dbUrl_ + "/" + path + ".json?auth=" + idToken_;
        if (!ns_.empty())
            url += "&ns=" + ns_;
        return request(url, method, body);
    }

    // method: "GET", "POST", "PUT" or "DELETE" (fixed strings, never user input)
    HttpResult request(const std::string &url, const char *method, const std::string *postBody)
    {
        const std::string reqFile = "sc_fb_req.json", bodyFile = "sc_fb_body.json",
                          codeFile = "sc_fb_code.txt", errFile = "sc_fb_err.txt";
        HttpResult r;

        std::string cmd = curlExe_ + " -s -S --connect-timeout 10 --max-time 30 -o \"" +
                          bodyFile + "\" -w \"%{http_code}\"";
        if (postBody)
        {
            // Password goes in a temp file (not the command line) and is deleted right after.
            std::ofstream rf(reqFile, std::ios::binary);
            rf << *postBody;
            rf.close();
            cmd += std::string(" -X ") + method +
                   " -H \"Content-Type: application/json\" --data-binary \"@" + reqFile + "\"";
        }
        else if (std::string(method) != "GET")
            cmd += std::string(" -X ") + method;
        cmd += " \"" + url + "\" > \"" + codeFile + "\" 2> \"" + errFile + "\"";

        int rc = std::system(cmd.c_str());
        if (postBody)
            std::remove(reqFile.c_str());

        r.body = readFile(bodyFile);
        r.status = std::atoi(readFile(codeFile).c_str());
        r.error = readFile(errFile);
        while (!r.error.empty() && (r.error.back() == '\n' || r.error.back() == '\r'))
            r.error.pop_back();
        if (r.status == 0 && r.error.empty())
            r.error = "curl exited with code " + std::to_string(rc) + " (is curl installed?)";

        std::remove(bodyFile.c_str());
        std::remove(codeFile.c_str());
        std::remove(errFile.c_str());
        return r;
    }

    static std::string base64UrlDecode(const std::string &in)
    {
        std::string out;
        int val = 0, bits = -8;
        for (char c : in)
        {
            int d;
            if (c >= 'A' && c <= 'Z') d = c - 'A';
            else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
            else if (c >= '0' && c <= '9') d = c - '0' + 52;
            else if (c == '-' || c == '+') d = 62;
            else if (c == '_' || c == '/') d = 63;
            else break; // padding or end
            val = (val << 6) | d;
            bits += 6;
            if (bits >= 0)
            {
                out += char((val >> bits) & 0xFF);
                bits -= 8;
            }
        }
        return out;
    }

    // Reads (does NOT verify) the JWT payload to show the hospital claim and
    // expiry. Verification is Firebase's job: the server checks the signature
    // on every request, so a tampered token is simply rejected.
    void decodeClaims()
    {
        hospital_.clear();
        expiry_ = 0;
        size_t a = idToken_.find('.');
        size_t b = idToken_.find('.', a + 1);
        if (a == std::string::npos || b == std::string::npos)
            return;
        try
        {
            JsonValue p = JsonParser::parse(base64UrlDecode(idToken_.substr(a + 1, b - a - 1)));
            const JsonValue *h = p.find("hospital");
            if (h && h->isString())
                hospital_ = h->text;
            const JsonValue *exp = p.find("exp");
            if (exp && exp->isNumber())
                expiry_ = static_cast<std::time_t>(exp->number);
        }
        catch (...)
        {
        }
    }
};

} // namespace smartcare

#endif
