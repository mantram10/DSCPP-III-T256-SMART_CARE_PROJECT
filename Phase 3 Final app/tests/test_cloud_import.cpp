// ============================================================
// test_cloud_import.cpp - SmartCare Phase 3 tests
//
// Build & run (from repository root):
//   g++ -std=c++17 -Wall -Wextra -I cloud tests/test_cloud_import.cpp -o test_cloud
//   ./test_cloud                 (unit tests, no network, no database)
//
// Optional end-to-end client test against the local mock server:
//   python tests/mock_firebase_server.py --faulty &      (port 8765)
//   SC_MOCK_URL=http://127.0.0.1:8765 ./test_cloud
// ============================================================
#include "cloud_menu.h"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace smartcare;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                                        \
    do                                                                                     \
    {                                                                                      \
        ++checks;                                                                          \
        if (!(cond))                                                                       \
        {                                                                                  \
            ++failures;                                                                    \
            std::cout << "  FAIL line " << __LINE__ << ": " #cond << '\n';                 \
        }                                                                                  \
    } while (0)

// ---- stand-ins for phase2.cpp classes ----
struct MockDB
{
    bool succeed = true;
    std::vector<std::string> executed;
    bool execute(std::string q)
    {
        executed.push_back(q);
        return succeed;
    }
    void showQuery(std::string q) { executed.push_back(q); }
};
struct Patient
{
    int id;
    std::string name;
    int age;
    char gender;
    Patient(int i, std::string n, int a, char g) : id(i), name(n), age(a), gender(g) {}
};

static bool parses(const std::string &s)
{
    try
    {
        JsonParser::parse(s);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

static void testParser()
{
    std::cout << "[parser]\n";
    JsonValue v = JsonParser::parse(R"({"a":{"b":[1,2.5,-3e2,true,false,null]},"s":"x\"y\\z\u00e9\ud83d\ude00"})");
    CHECK(v.isObject());
    const JsonValue *arr = v.find("a")->find("b");
    CHECK(arr && arr->isArray() && arr->items.size() == 6);
    int n;
    CHECK(arr->items[0].asInt(n) && n == 1);
    CHECK(!arr->items[1].asInt(n)); // 2.5 is not an int
    CHECK(arr->items[2].number == -300);
    CHECK(arr->items[3].boolean && !arr->items[4].boolean && arr->items[5].isNull());
    CHECK(v.find("s")->text == "x\"y\\z\xC3\xA9\xF0\x9F\x98\x80");
    CHECK(JsonParser::parse("null").isNull());
    CHECK(JsonParser::parse("  {}  ").isObject());

    CHECK(!parses(""));
    CHECK(!parses("{\"a\":1,}"));       // trailing comma
    CHECK(!parses("{\"a\":1} x"));      // trailing garbage
    CHECK(!parses("\"unterminated"));
    CHECK(!parses("01"));               // leading zero
    CHECK(!parses("\"a\nb\""));         // raw control character
    CHECK(!parses("\"\\ud800\""));      // unpaired surrogate
    CHECK(!parses(std::string(100, '[') + std::string(100, ']'))); // too deep
}

static const char *DATASET = R"({
  "P120001": {"patient_id":120001,"name":"Riya Negi","age":34,"gender":"F"},
  "P120002": {"patient_id":120002,"name":"  Arjun O'Brien ","age":0,"gender":"M"},
  "P120003": {"patient_id":120003,"name":"","age":30,"gender":"F"},
  "P120004": {"patient_id":120004,"name":"Old","age":121,"gender":"M"},
  "P120005": {"patient_id":120005,"name":"G","age":40,"gender":"X"},
  "P110050": {"patient_id":110050,"name":"Wrong Range","age":40,"gender":"F"},
  "Pabc":    {"patient_id":"abc","name":"Bad","age":1,"gender":"F"},
  "P120006": {"patient_id":120007,"name":"Key mismatch","age":1,"gender":"F"},
  "P120008": {"patient_id":120008,"name":"Bad;Name","age":1,"gender":"F"},
  "P120009": {"patient_id":120009,"name":"Exists","age":50,"gender":"O"},
  "P120010": "not an object",
  "P120011": {"patient_id":120011,"name":"Float age","age":30.5,"gender":"M"}
})";

static void testValidation()
{
    std::cout << "[validation]\n";
    JsonValue root = JsonParser::parse(DATASET);
    ImportReport r = validatePatients(root, ImportMode::HospitalDataset, "Hospital_B",
                                      [](int id) { return id == 120009; });
    CHECK(r.total == 12);
    CHECK(r.accepted.size() == 2);
    CHECK(r.accepted.size() + r.issues.size() == r.total);
    CHECK(r.accepted[1].name == "Arjun O'Brien"); // trimmed
    int skipped = 0;
    for (auto &i : r.issues)
        if (i.status == "SKIPPED")
            ++skipped;
    CHECK(skipped == 1);
    for (auto &i : r.issues)
        std::cout << "    " << i.status << " " << i.recordKey << ": " << i.reason << '\n';

    // Reading Hospital_B data while claiming it is Hospital_A -> range check rejects all
    ImportReport wrong = validatePatients(root, ImportMode::HospitalDataset, "Hospital_A", nullptr);
    CHECK(wrong.accepted.size() == 1); // only P110050 fits A's range
    CHECK(wrong.accepted[0].id == 110050);

    CHECK(validatePatients(JsonParser::parse("null"), ImportMode::HospitalDataset, "Hospital_A", nullptr).total == 0);
    CHECK(!validatePatients(JsonParser::parse("[1,2]"), ImportMode::HospitalDataset, "Hospital_A", nullptr).fatalError.empty());
    CHECK(!validatePatients(JsonParser::parse("42"), ImportMode::HospitalDataset, "Hospital_A", nullptr).fatalError.empty());
}

static void testShared()
{
    std::cout << "[shared records]\n";
    JsonValue root = JsonParser::parse(R"({
      "P110005": {"patient_id":110005,"name":"Shared Ok","age":20,"gender":"F","source_hospital":"Hospital_A","shared_with":"Hospital_B","consent":true},
      "P110006": {"patient_id":110006,"name":"No Consent","age":20,"gender":"F","source_hospital":"Hospital_A","shared_with":"Hospital_B","consent":false},
      "P110007": {"patient_id":110007,"name":"Wrong Target","age":20,"gender":"F","source_hospital":"Hospital_A","shared_with":"Hospital_C","consent":true},
      "P120001": {"patient_id":120001,"name":"Own","age":20,"gender":"F","source_hospital":"Hospital_B","shared_with":"Hospital_B","consent":true},
      "P110008": {"patient_id":110008,"name":"Consent str","age":20,"gender":"F","source_hospital":"Hospital_A","shared_with":"Hospital_B","consent":"true"},
      "P120002": {"patient_id":120002,"name":"Lies about source","age":20,"gender":"F","source_hospital":"Hospital_A","shared_with":"Hospital_B","consent":true}
    })");
    ImportReport r = validatePatients(root, ImportMode::SharedRecords, "Hospital_B", nullptr);
    CHECK(r.accepted.size() == 1);
    CHECK(r.accepted.size() == 1 && r.accepted[0].sourceHospital == "Hospital_A");
    for (auto &i : r.issues)
        std::cout << "    " << i.status << " " << i.recordKey << ": " << i.reason << '\n';
}

static void testSql()
{
    std::cout << "[sql]\n";
    JsonValue root = JsonParser::parse(DATASET);
    ImportReport r = validatePatients(root, ImportMode::HospitalDataset, "Hospital_B", nullptr);
    std::string sql = buildImportSql(r, "SmartCare/Hospital_B/Patients");
    CHECK(sql.rfind("BEGIN;", 0) == 0);
    CHECK(sql.find("COMMIT;") != std::string::npos);
    CHECK(sql.find("'Arjun O''Brien'") != std::string::npos); // quote escaped
    CHECK(sql.find("ON CONFLICT (patient_id) DO NOTHING") != std::string::npos);
    CHECK(sql.find("'FIREBASE:Hospital_B'") != std::string::npos);
    CHECK(sql.find("'REJECTED'") != std::string::npos);
}

static void testClientGuards()
{
    std::cout << "[client guards]\n";
    CHECK(FirebaseClient("", "k").configProblem() != "");
    CHECK(FirebaseClient("http://example.com", "k").configProblem() != "");   // not https
    CHECK(FirebaseClient("https://x.app\" & del", "k").configProblem() != ""); // shell chars
    CHECK(FirebaseClient("https://x.firebasedatabase.app", "a&b").configProblem() != "");
    CHECK(FirebaseClient("https://x.firebasedatabase.app", "AIzaSy-ok_1").configProblem() == "");
    FirebaseClient fb("https://x.firebasedatabase.app", "key");
    CHECK(fb.get("SmartCare/Hospital_A").status == 0); // not signed in -> refused locally
}

// End-to-end against tests/mock_firebase_server.py --faulty
static void testEndToEnd(const std::string &url)
{
    std::cout << "[end-to-end via mock server " << url << "]\n";
    FirebaseClient fb(url, "test-key", url);
    std::string msg;

    CHECK(!fb.signIn("b@smartcare.test", "wrong", msg));
    std::cout << "    " << msg << '\n';
    CHECK(msg.find("INVALID_LOGIN_CREDENTIALS") != std::string::npos);

    CHECK(fb.signIn("b@smartcare.test", "test123", msg));
    std::cout << "    " << msg << '\n';
    CHECK(fb.hospital() == "Hospital_B");

    HttpResult denied = fb.get("SmartCare/Hospital_A/Patients");
    CHECK(denied.status == 401);

    MockDB db;
    std::list<Patient> patients;
    std::unordered_map<int, Patient> hash;
    std::stack<std::string> actions;
    patients.push_back(Patient(120002, "Already Here", 30, 'M'));
    hash.emplace(120002, patients.back());

    std::istringstream input("y\ny\n");
    std::streambuf *old = std::cin.rdbuf(input.rdbuf());
    std::ostringstream out;
    std::streambuf *oldOut = std::cout.rdbuf(out.rdbuf());

    cloudImportPatients(fb, db, "SmartCare/Hospital_B/Patients", ImportMode::HospitalDataset,
                        "Hospital_B", patients, hash, actions);
    size_t afterDataset = patients.size();
    cloudImportPatients(fb, db, "SmartCare/Shared_Records/Hospital_B", ImportMode::SharedRecords,
                        "Hospital_B", patients, hash, actions);
    cloudImportPatients(fb, db, "SmartCare/Hospital_A/Patients", ImportMode::HospitalDataset,
                        "Hospital_A", patients, hash, actions);

    std::cin.rdbuf(old);
    std::cout.rdbuf(oldOut);
    std::cout << out.str();

    // Faulty file: 5 valid + 8 bad; 120002 already exists -> 4 imported
    CHECK(afterDataset == 1 + 4);
    CHECK(patients.size() == 1 + 4 + 1); // + P110005 shared with consent
    CHECK(hash.count(110005) == 1 && hash.count(110006) == 0);
    CHECK(out.str().find("ACCESS DENIED (HTTP 401)") != std::string::npos);
    CHECK(db.executed.size() == 2);

    // Database failure: memory must stay unchanged
    MockDB failing;
    failing.succeed = false;
    std::list<Patient> p2;
    std::unordered_map<int, Patient> h2;
    std::istringstream in2("y\n");
    old = std::cin.rdbuf(in2.rdbuf());
    oldOut = std::cout.rdbuf(out.rdbuf());
    cloudImportPatients(fb, failing, "SmartCare/Shared_Records/Hospital_B", ImportMode::SharedRecords,
                        "Hospital_B", p2, h2, actions);
    std::cin.rdbuf(old);
    std::cout.rdbuf(oldOut);
    CHECK(p2.empty() && h2.empty());

    // ---------- Sharing from the app ----------
    FirebaseClient fa(url, "test-key", url);
    CHECK(fa.signIn("a@smartcare.test", "test123", msg));
    CHECK(fa.hospital() == "Hospital_A");
    std::stack<std::string> acts;
    {
        // target 1 (Hospital_B), patient 110010, consent n -> refused locally
        std::istringstream in("1\n110010\nn\n");
        std::ostringstream o;
        std::streambuf *ib = std::cin.rdbuf(in.rdbuf());
        std::streambuf *ob = std::cout.rdbuf(o.rdbuf());
        cloudSharePatient(fa, acts);
        // consent y -> shared
        std::istringstream in2("1\n110010\ny\n");
        std::cin.rdbuf(in2.rdbuf());
        cloudSharePatient(fa, acts);
        // patient of another hospital -> refused locally
        std::istringstream in3("1\n120001\n");
        std::cin.rdbuf(in3.rdbuf());
        cloudSharePatient(fa, acts);
        std::cin.rdbuf(ib);
        std::cout.rdbuf(ob);
        std::cout << o.str();
        CHECK(o.str().find("NOT shared: patient consent is required") != std::string::npos);
        CHECK(o.str().find("SHARED P110010 from Hospital_A to Hospital_B") != std::string::npos);
        CHECK(o.str().find("You can only share patients of Hospital_A") != std::string::npos);
    }
    // Server-side rules: tampered data and wrong source are denied even without the app's checks
    CHECK(fa.put("SmartCare/Shared_Records/Hospital_B/P110011",
                 "{\"patient_id\":110011,\"name\":\"FAKE NAME\",\"age\":1,\"gender\":\"M\","
                 "\"source_hospital\":\"Hospital_A\",\"shared_with\":\"Hospital_B\",\"consent\":true}").status == 401);
    CHECK(fa.put("SmartCare/Shared_Records/Hospital_A/P110010", "{}").status == 401); // share with itself
    CHECK(fa.put("SmartCare/Hospital_B/Patients/P120001", "{}").status == 401);        // write other hospital's data
    CHECK(fb.remove("SmartCare/Shared_Records/Hospital_B/P110010").status == 401);    // B cannot revoke A's share

    // Hospital B imports the new share
    {
        std::istringstream in("y\n");
        std::ostringstream o;
        std::streambuf *ib = std::cin.rdbuf(in.rdbuf());
        std::streambuf *ob = std::cout.rdbuf(o.rdbuf());
        cloudImportPatients(fb, db, "SmartCare/Shared_Records/Hospital_B", ImportMode::SharedRecords,
                            "Hospital_B", patients, hash, actions);
        std::cin.rdbuf(ib);
        std::cout.rdbuf(ob);
        CHECK(hash.count(110010) == 1);
    }
    // Revoke by Hospital A
    {
        std::istringstream in("1\n110010\n1\n110010\n");
        std::ostringstream o;
        std::streambuf *ib = std::cin.rdbuf(in.rdbuf());
        std::streambuf *ob = std::cout.rdbuf(o.rdbuf());
        cloudRevokeShare(fa, acts);
        cloudRevokeShare(fa, acts); // second time: nothing to revoke -> denied
        std::cin.rdbuf(ib);
        std::cout.rdbuf(ob);
        std::cout << o.str();
        CHECK(o.str().find("REVOKED P110010") != std::string::npos);
        CHECK(o.str().find("DENIED (HTTP 401)") != std::string::npos);
    }

    // Network failure: nothing listening on port 1
    FirebaseClient dead("http://127.0.0.1:1", "test-key", "http://127.0.0.1:1");
    CHECK(!dead.signIn("b@smartcare.test", "test123", msg));
    std::cout << "    network failure -> " << msg << '\n';
}

int main()
{
    testParser();
    testValidation();
    testShared();
    testSql();
    testClientGuards();
    std::string mock = readEnv("SC_MOCK_URL");
    if (!mock.empty())
        testEndToEnd(mock);
    else
        std::cout << "[end-to-end] skipped (set SC_MOCK_URL to run)\n";

    std::cout << "\n" << (checks - failures) << "/" << checks << " checks passed\n";
    return failures ? 1 : 0;
}
