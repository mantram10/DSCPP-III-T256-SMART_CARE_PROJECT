// SmartCare - Modern local Windows GUI (Win32 API)  -- corrected version
// Target: Dev-C++ 5.11 / MinGW-w64, C++98 style.
// Database access uses PostgreSQL's psql.exe (no libpq linking needed).
//
// Optional settings file "smartcare.ini" next to the .exe:
//   [db]
//   psql=C:\Program Files\PostgreSQL\18\bin\psql.exe
//   host=localhost
//   port=5432
//   name=smartcare
//   user=postgres
//
// Fixes vs. the original:
//  - sqlQuote no longer doubles backslashes (data corruption)
//  - read-only output box and card labels get correct colours
//  - psql runs without freezing the UI (message pump + timeout, -w, connect timeout)
//  - Tab / Enter keyboard navigation, minimum window size, WS_CLIPCHILDREN
//  - output limit lifted, aligned tables in a monospaced font
//  - appointments/emergencies are marked 'done' instead of deleted (history kept)
//  - friendly "does not exist" errors for patient/doctor IDs
//  - BFS/DFS really traverse an adjacency list
//  - password box cleared and zeroed after Connect; psql path validated

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <cctype>

using namespace std;

// Defaults; overridden by smartcare.ini if present.
static string PSQL_PATH = "C:\\Program Files\\PostgreSQL\\18\\bin\\psql.exe";
static string DB_HOST = "localhost";
static string DB_PORT = "5432";
static string DB_NAME = "smartcare";
static string DB_USER = "postgres";

#define ID_PASSWORD 101
#define ID_STATUS 102
#define ID_ID 103
#define ID_NAME 104
#define ID_AGE 105
#define ID_GENDER 106
#define ID_SPEC 107
#define ID_DOCTOR 108
#define ID_PRIORITY 109
#define ID_DIAGNOSIS 110
#define ID_PRESCRIPTION 111
#define ID_OUTPUT 112
#define ID_CONNECT 201
#define ID_PATIENTS 202
#define ID_DOCTORS 203
#define ID_APPOINTMENTS 204
#define ID_EMERGENCIES 205
#define ID_RECORDS 206
#define ID_ADD_PATIENT 207
#define ID_ADD_DOCTOR 208
#define ID_BOOK 209
#define ID_ADD_EMERGENCY 210
#define ID_ADD_RECORD 211
#define ID_CLEAR 212
#define ID_BFS 213
#define ID_DFS 214
#define ID_NETWORK 215
#define ID_OVERVIEW 216
#define ID_PROCESS_APPT 217
#define ID_PROCESS_EMERGENCY 218
#define ID_BRAND 301
#define ID_SUBTITLE 302

#define PSQL_TIMEOUT_MS 30000

HWND gMain = NULL, gPassword = NULL, gStatus = NULL, gOutput = NULL;
HWND gId = NULL, gName = NULL, gAge = NULL, gGender = NULL, gSpec = NULL;
HWND gDoctor = NULL, gPriority = NULL, gDiagnosis = NULL, gPrescription = NULL;
HFONT gFont = NULL, gBoldFont = NULL, gTitleFont = NULL, gMonoFont = NULL;
HBRUSH gBackground = NULL, gWhiteBrush = NULL, gHeaderBrush = NULL;
COLORREF C_BG = RGB(242,246,252);
COLORREF C_NAVY = RGB(22,35,62);
COLORREF C_TEXT = RGB(32,45,67);
COLORREF C_TEAL = RGB(0,150,136);
COLORREF C_BLUE = RGB(54,104,210);
COLORREF C_PURPLE = RGB(117,82,190);
COLORREF C_ORANGE = RGB(226,137,45);
COLORREF C_RED = RGB(208,73,82);
COLORREF C_GREEN = RGB(37,150,100);
bool gConnected = false;
bool gBusy = false;

string trim(const string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// PostgreSQL writes LF; the Win32 EDIT control needs CRLF.
string toWindowsNewlines(const string &s) {
    string out;
    out.reserve(s.size() + 32);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out += "\r\n";
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else if (s[i] == '\n') {
            out += "\r\n";
        } else {
            out += s[i];
        }
    }
    return out;
}

// With standard_conforming_strings=on (default since PostgreSQL 9.1) a backslash
// is an ordinary character inside '...'; only the single quote needs escaping.
string sqlQuote(const string &s) {
    string r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\'') r += "''";
        else if (s[i] == '\0') continue;
        else r += s[i];
    }
    return r;
}

// Digits only, at most 9 digits (fits a 32-bit INTEGER; avoids atol overflow).
bool isPositiveInteger(const string &s, bool allowZero = false) {
    if (s.empty() || s.size() > 9) return false;
    for (size_t i = 0; i < s.size(); ++i) if (!isdigit((unsigned char)s[i])) return false;
    long v = atol(s.c_str());
    return allowZero ? v >= 0 : v > 0;
}

string getText(HWND h) {
    int n = GetWindowTextLengthA(h);
    vector<char> b(n + 1);
    GetWindowTextA(h, &b[0], n + 1);
    return string(&b[0]);
}
void setOutput(const string &s) { SetWindowTextA(gOutput, s.c_str()); }
void setStatus(const string &s) { SetWindowTextA(gStatus, s.c_str()); }

void loadConfig() {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    string ini(path);
    size_t slash = ini.find_last_of("\\/");
    ini = (slash == string::npos ? string("") : ini.substr(0, slash + 1)) + "smartcare.ini";
    if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    char buf[512];
    GetPrivateProfileStringA("db", "psql", PSQL_PATH.c_str(), buf, sizeof(buf), ini.c_str()); PSQL_PATH = buf;
    GetPrivateProfileStringA("db", "host", DB_HOST.c_str(), buf, sizeof(buf), ini.c_str()); DB_HOST = buf;
    GetPrivateProfileStringA("db", "port", DB_PORT.c_str(), buf, sizeof(buf), ini.c_str()); DB_PORT = buf;
    GetPrivateProfileStringA("db", "name", DB_NAME.c_str(), buf, sizeof(buf), ini.c_str()); DB_NAME = buf;
    GetPrivateProfileStringA("db", "user", DB_USER.c_str(), buf, sizeof(buf), ini.c_str()); DB_USER = buf;
}

// Runs psql with the SQL in a temp file. The UI stays responsive (messages are
// pumped while waiting) and a hung connection is killed after PSQL_TIMEOUT_MS.
bool runPSQL(const string &sql, string &output) {
    if (!gConnected) { output = "Connect to PostgreSQL first.\r\n"; return false; }
    if (gBusy) { output = "Another operation is still running."; return false; }

    char tempDir[MAX_PATH];
    DWORD tempLen = GetTempPathA(MAX_PATH, tempDir);
    if (tempLen == 0 || tempLen >= MAX_PATH) {
        output = "Windows could not determine the temporary folder."; return false;
    }
    char sqlPath[MAX_PATH] = {0}, outPath[MAX_PATH] = {0};
    if (!GetTempFileNameA(tempDir, "scs", 0, sqlPath) ||
        !GetTempFileNameA(tempDir, "sco", 0, outPath)) {
        output = "Could not create temporary files. Check Windows TEMP folder permissions."; return false;
    }

    ofstream f(sqlPath, ios::out | ios::binary);
    if (!f) {
        output = "Could not write temporary SQL file.";
        DeleteFileA(sqlPath); DeleteFileA(outPath); return false;
    }
    f << "SET client_min_messages TO WARNING;\n" << sql << "\n";
    f.close();

    SECURITY_ATTRIBUTES sa; ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE hOut = CreateFileA(outPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut == INVALID_HANDLE_VALUE) {
        output = "Could not capture PostgreSQL output.";
        DeleteFileA(sqlPath); DeleteFileA(outPath); return false;
    }

    // -w: never prompt for a password (a prompt would hang a hidden process).
    // Default (aligned) output format is used so tables read well in a mono font.
    string command = "\"" + PSQL_PATH + "\" -X -w -h " + DB_HOST + " -p " + DB_PORT +
                     " -U " + DB_USER + " -d " + DB_NAME +
                     " -v ON_ERROR_STOP=1 -P footer=off -f \"" + sqlPath + "\"";

    STARTUPINFOA si; PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si)); ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si); si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; si.hStdOutput = hOut; si.hStdError = hOut;
    HANDLE hIn = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    si.hStdInput = hIn;

    vector<char> cmd(command.begin(), command.end()); cmd.push_back('\0');
    BOOL created = CreateProcessA(PSQL_PATH.c_str(), &cmd[0], NULL, NULL, TRUE,
                                  CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(hOut);
    if (hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);
    if (!created) {
        DWORD err = GetLastError();
        ostringstream msg; msg << "Could not start psql.exe (Windows error " << err << ").\r\n"
            << "Expected path: " << PSQL_PATH << "\r\nCheck that this file exists, or set it in smartcare.ini.";
        output = msg.str(); DeleteFileA(sqlPath); DeleteFileA(outPath); return false;
    }

    gBusy = true;
    HCURSOR oldCursor = SetCursor(LoadCursor(NULL, IDC_WAIT));
    DWORD start = GetTickCount();
    bool timedOut = false, quitting = false;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 100, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0) break;
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_QUIT) { quitting = true; PostQuitMessage((int)m.wParam); break; }
            TranslateMessage(&m); DispatchMessageA(&m);
        }
        if (quitting) { TerminateProcess(pi.hProcess, 1); break; }
        if (GetTickCount() - start > PSQL_TIMEOUT_MS) { timedOut = true; TerminateProcess(pi.hProcess, 1); break; }
    }
    WaitForSingleObject(pi.hProcess, 2000);
    DWORD exitCode = 1; GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    SetCursor(oldCursor);
    gBusy = false;

    ifstream in(outPath, ios::in | ios::binary);
    ostringstream buffer; buffer << in.rdbuf(); output = toWindowsNewlines(buffer.str());
    in.close();
    DeleteFileA(sqlPath); DeleteFileA(outPath);
    if (timedOut) { output = "PostgreSQL did not respond within 30 seconds. Is the server running?\r\n\r\n" + output; return false; }
    if (output.empty()) output = (exitCode == 0 ? "Command completed successfully." : "PostgreSQL returned an error without output.");
    return exitCode == 0;
}

bool execSQL(const string &sql, const string &successMessage) {
    string out;
    bool ok = runPSQL(sql, out);
    if (ok) { setOutput(successMessage + "\r\n\r\n" + out); setStatus("Connected  |  Operation completed"); }
    else { setOutput("Operation failed.\r\n\r\n" + out); setStatus("Database operation failed"); }
    return ok;
}

void createSchema() {
    // Additive schema setup: never drops tables or columns.
    string sql =
        "CREATE TABLE IF NOT EXISTS patients (patient_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL, age INTEGER CHECK(age BETWEEN 0 AND 125), gender CHAR(1) CHECK(gender IN ('M','F','U')));\n"
        "CREATE TABLE IF NOT EXISTS doctors (doctor_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL, specialization VARCHAR(100));\n"
        "CREATE TABLE IF NOT EXISTS appointments (appointment_id SERIAL PRIMARY KEY, patient_id INTEGER REFERENCES patients(patient_id), doctor_id INTEGER REFERENCES doctors(doctor_id));\n"
        "CREATE TABLE IF NOT EXISTS emergency (emergency_id SERIAL PRIMARY KEY, patient_id INTEGER REFERENCES patients(patient_id), priority INTEGER CHECK(priority BETWEEN 1 AND 5));\n"
        "CREATE TABLE IF NOT EXISTS medical_records (record_id SERIAL PRIMARY KEY, patient_id INTEGER REFERENCES patients(patient_id), diagnosis VARCHAR(200), prescription VARCHAR(300));\n"
        "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS diagnosis VARCHAR(200);\n"
        "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS prescription VARCHAR(300);\n"
        // History-preserving queue columns.
        "ALTER TABLE appointments ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'pending';\n"
        "ALTER TABLE appointments ADD COLUMN IF NOT EXISTS created_at TIMESTAMP NOT NULL DEFAULT now();\n"
        "ALTER TABLE emergency ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'pending';\n"
        "ALTER TABLE emergency ADD COLUMN IF NOT EXISTS created_at TIMESTAMP NOT NULL DEFAULT now();\n"
        "DO $smartcare$ BEGIN "
        "IF EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema='public' AND table_name='medical_records' AND column_name='disease') THEN "
        "EXECUTE 'UPDATE medical_records SET diagnosis=COALESCE(diagnosis, disease) WHERE diagnosis IS NULL'; END IF; "
        "IF EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema='public' AND table_name='medical_records' AND column_name='treatment') THEN "
        "EXECUTE 'UPDATE medical_records SET prescription=COALESCE(prescription, treatment) WHERE prescription IS NULL'; "
        "ELSIF EXISTS (SELECT 1 FROM information_schema.columns WHERE table_schema='public' AND table_name='medical_records' AND column_name='medicine') THEN "
        "EXECUTE 'UPDATE medical_records SET prescription=COALESCE(prescription, medicine) WHERE prescription IS NULL'; END IF; "
        "END; $smartcare$;";
    string out;
    if (runPSQL(sql, out)) {
        setStatus("Connected to " + DB_NAME + "  |  Ready");
        setOutput("Welcome back to SmartCare!\r\n\r\nDatabase connection successful.\r\nYour existing tables and records were preserved.\r\n\r\nChoose a dashboard action to view records, or use Quick Actions to add data.");
    } else {
        gConnected = false;
        setStatus("Connection failed  |  Check password/settings");
        setOutput("Could not connect to PostgreSQL.\r\n\r\nCheck that PostgreSQL is running, the password is correct, and psql.exe exists at:\r\n" + PSQL_PATH + "\r\n\r\nDatabase error:\r\n" + out);
    }
}

void query(const string &sql, const string &title) {
    string out;
    if (runPSQL(sql, out)) { setOutput(title + "\r\n" + string(60, '=') + "\r\n\r\n" + (trim(out).empty() ? "No records found." : out)); setStatus("Query completed"); }
    else { setOutput("Query failed: " + title + "\r\n\r\n" + out); setStatus("Query failed"); }
}

void label(HWND parent, const char *text, int x, int y, int w = 100) {
    HWND h = CreateWindowA("STATIC", text, WS_CHILD|WS_VISIBLE, x, y, w, 22, parent, NULL, NULL, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)gFont, TRUE);
}
HWND edit(HWND parent, int id, int x, int y, int w, int maxLen, bool password = false) {
    HWND h = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL|(password?ES_PASSWORD:0), x, y, w, 25, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(h, WM_SETFONT, (WPARAM)gFont, TRUE);
    SendMessageA(h, EM_LIMITTEXT, maxLen, 0);
    return h;
}
void button(HWND parent, const char *text, int id, int x, int y, int w, int h = 34) {
    HWND b = CreateWindowA("BUTTON", text, WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW, x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageA(b, WM_SETFONT, (WPARAM)gFont, TRUE);
}
void heading(HWND parent, const char *text, int x, int y, int w, int h) {
    HWND t = CreateWindowA("STATIC", text, WS_CHILD|WS_VISIBLE, x, y, w, h, parent, NULL, NULL, NULL);
    SendMessageA(t, WM_SETFONT, (WPARAM)gBoldFont, TRUE);
}

void layoutControls(HWND hwnd) {
    RECT r; GetClientRect(hwnd, &r);
    int cw = r.right, ch = r.bottom;
    int rightX = 375;
    int rightW = cw - rightX - 24;
    if (rightW < 420) rightW = 420;
    int gap = 10;
    int bw = (rightW - 2 * gap) / 3;
    int ids[9] = {ID_PATIENTS, ID_DOCTORS, ID_APPOINTMENTS, ID_EMERGENCIES, ID_RECORDS, ID_NETWORK, ID_BFS, ID_DFS, ID_OVERVIEW};
    int rowY[3] = {178, 217, 256};
    for (int i = 0; i < 9; ++i) {
        int col = i % 3, row = i / 3;
        HWND h = GetDlgItem(hwnd, ids[i]);
        if (h) MoveWindow(h, rightX + col*(bw+gap), rowY[row], bw, 34, TRUE);
    }
    if (gOutput) MoveWindow(gOutput, rightX, 296, rightW, (ch > 330 ? ch - 320 : 300), TRUE);
    HWND status = GetDlgItem(hwnd, ID_STATUS);
    if (status) MoveWindow(status, 420, 111, cw - 440, 24, TRUE);
}

// ---- input actions ---------------------------------------------------------

void addPatient() {
    string id=getText(gId), name=trim(getText(gName)), age=getText(gAge), gender=trim(getText(gGender));
    if (id.empty() || name.empty() || age.empty() || gender.empty()) { setOutput("For Add Patient, fill Patient ID, Name, Age and Gender."); return; }
    if (!isPositiveInteger(id) || !isPositiveInteger(age, true) || atoi(age.c_str()) > 125) { setOutput("Patient ID must be positive and age must be between 0 and 125."); return; }
    if (gender.size()!=1 || (toupper((unsigned char)gender[0])!='M' && toupper((unsigned char)gender[0])!='F' && toupper((unsigned char)gender[0])!='U')) { setOutput("Gender should be M, F or U."); return; }
    gender[0] = (char)toupper((unsigned char)gender[0]);
    string sql="INSERT INTO patients(patient_id,name,age,gender) VALUES ("+id+", '"+sqlQuote(name)+"', "+age+", '"+sqlQuote(gender)+"');";
    execSQL(sql, "Patient saved successfully.\r\n\r\nTip: choose Patients to refresh the directory.");
}
void addDoctor() {
    string id=getText(gId), name=trim(getText(gName)), spec=trim(getText(gSpec));
    if (id.empty() || name.empty() || spec.empty()) { setOutput("For Add Doctor, fill ID, Name and Specialization."); return; }
    if (!isPositiveInteger(id)) { setOutput("Doctor ID must be a positive integer."); return; }
    string sql="INSERT INTO doctors(doctor_id,name,specialization) VALUES ("+id+", '"+sqlQuote(name)+"', '"+sqlQuote(spec)+"');";
    execSQL(sql, "Doctor saved successfully.\r\n\r\nTip: choose Doctors to refresh the directory.");
}

// SQL fragment that aborts with a readable message if a patient/doctor ID is unknown.
string requirePatient(const string &pid) {
    return "DO $chk$ BEGIN IF NOT EXISTS (SELECT 1 FROM patients WHERE patient_id=" + pid +
           ") THEN RAISE EXCEPTION 'Patient ID " + pid + " does not exist. Add the patient first.'; END IF; END $chk$;\n";
}
string requireDoctor(const string &did) {
    return "DO $chk$ BEGIN IF NOT EXISTS (SELECT 1 FROM doctors WHERE doctor_id=" + did +
           ") THEN RAISE EXCEPTION 'Doctor ID " + did + " does not exist. Add the doctor first.'; END IF; END $chk$;\n";
}

void bookAppointment() {
    string pid=getText(gId), did=getText(gDoctor);
    if(pid.empty()||did.empty()){setOutput("Enter Patient ID in the first field and Doctor ID in the Doctor ID field.");return;}
    if(!isPositiveInteger(pid)||!isPositiveInteger(did)){setOutput("Patient ID and Doctor ID must be positive integers.");return;}
    string sql = requirePatient(pid) + requireDoctor(did) +
        "DO $chk$ BEGIN IF EXISTS (SELECT 1 FROM appointments WHERE patient_id=" + pid + " AND doctor_id=" + did +
        " AND status='pending') THEN RAISE EXCEPTION 'This patient already has a pending appointment with this doctor.'; END IF; END $chk$;\n"
        "INSERT INTO appointments(patient_id,doctor_id) VALUES (" + pid + ", " + did + ");";
    execSQL(sql, "Appointment booked.");
}
void addEmergency() {
    string pid=getText(gId), pr=getText(gPriority);
    if(pid.empty()||pr.empty()){setOutput("Enter Patient ID and priority from 1 to 5.");return;}
    if(!isPositiveInteger(pid)||!isPositiveInteger(pr)){setOutput("Patient ID and priority must be positive integers.");return;}
    int p=atoi(pr.c_str()); if(p<1||p>5){setOutput("Priority must be 1 (Critical) through 5 (Low).");return;}
    execSQL(requirePatient(pid) + "INSERT INTO emergency(patient_id,priority) VALUES ("+pid+", "+pr+");", "Emergency case saved. Priority 1 is highest.");
}
void addRecord() {
    string pid=getText(gId), diagnosis=trim(getText(gDiagnosis)), prescription=trim(getText(gPrescription));
    if(pid.empty()||diagnosis.empty()||prescription.empty()){setOutput("Enter Patient ID, Diagnosis and Prescription.");return;}
    if(!isPositiveInteger(pid)){setOutput("Patient ID must be a positive integer.");return;}
    execSQL(requirePatient(pid) + "INSERT INTO medical_records(patient_id,diagnosis,prescription) VALUES ("+pid+", '"+sqlQuote(diagnosis)+"', '"+sqlQuote(prescription)+"');", "Medical record saved.");
}

// ---- hospital network: real BFS / DFS over an adjacency list ---------------

typedef map<string, vector<string> > Graph;

Graph buildNetwork(string &edgeText) {
    const char *edges[][2] = {
        {"City Hospital","Apollo Hospital"}, {"Apollo Hospital","Fortis Hospital"},
        {"Fortis Hospital","Max Hospital"}, {"City Hospital","AIIMS"}
    };
    Graph g;
    edgeText.clear();
    for (size_t i = 0; i < sizeof(edges)/sizeof(edges[0]); ++i) {
        g[edges[i][0]].push_back(edges[i][1]);
        g[edges[i][1]].push_back(edges[i][0]);
        edgeText += string(edges[i][0]) + " <-> " + edges[i][1] + "\r\n";
    }
    return g;
}
string bfs(const Graph &g, const string &start) {
    set<string> seen; queue<string> q; vector<string> order;
    seen.insert(start); q.push(start);
    while (!q.empty()) {
        string u = q.front(); q.pop(); order.push_back(u);
        Graph::const_iterator it = g.find(u);
        if (it == g.end()) continue;
        for (size_t i = 0; i < it->second.size(); ++i)
            if (seen.insert(it->second[i]).second) q.push(it->second[i]);
    }
    string s;
    for (size_t i = 0; i < order.size(); ++i) { if (i) s += " -> "; s += order[i]; }
    return s;
}
void dfsVisit(const Graph &g, const string &u, set<string> &seen, vector<string> &order) {
    seen.insert(u); order.push_back(u);
    Graph::const_iterator it = g.find(u);
    if (it == g.end()) return;
    for (size_t i = 0; i < it->second.size(); ++i)
        if (!seen.count(it->second[i])) dfsVisit(g, it->second[i], seen, order);
}
string dfs(const Graph &g, const string &start) {
    set<string> seen; vector<string> order;
    dfsVisit(g, start, seen, order);
    string s;
    for (size_t i = 0; i < order.size(); ++i) { if (i) s += " -> "; s += order[i]; }
    return s;
}

// ---- window procedure -------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        gFont = CreateFontA(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
        gBoldFont = CreateFontA(19,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
        gTitleFont = CreateFontA(25,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
        gMonoFont = CreateFontA(16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Consolas");
        gBackground = CreateSolidBrush(C_BG);
        gWhiteBrush = CreateSolidBrush(RGB(255,255,255));
        gHeaderBrush = CreateSolidBrush(C_NAVY);
        HWND brand = CreateWindowA("STATIC", "SMARTCARE  |  HEALTHCARE MANAGEMENT", WS_CHILD|WS_VISIBLE, 22, 15, 620, 34, hwnd, (HMENU)ID_BRAND, NULL, NULL);
        SendMessageA(brand, WM_SETFONT, (WPARAM)gTitleFont, TRUE);
        HWND subtitle = CreateWindowA("STATIC", "Next-Gen Digital Healthcare   |   Local desktop edition   |   Synthetic demo data", WS_CHILD|WS_VISIBLE, 24, 49, 620, 24, hwnd, (HMENU)ID_SUBTITLE, NULL, NULL);
        SendMessageA(subtitle, WM_SETFONT, (WPARAM)gFont, TRUE);
        heading(hwnd,"DATABASE CONNECTION",22,83,250,25);
        label(hwnd,"DB password",22,112,90); gPassword=edit(hwnd,ID_PASSWORD,112,108,190,128,true);
        button(hwnd,"Connect",ID_CONNECT,312,106,92);
        gStatus=CreateWindowA("STATIC","Not connected  |  Enter your PostgreSQL password",WS_CHILD|WS_VISIBLE,420,111,700,24,hwnd,(HMENU)(INT_PTR)ID_STATUS,NULL,NULL); SendMessageA(gStatus,WM_SETFONT,(WPARAM)gFont,TRUE);
        heading(hwnd,"DATA ENTRY",22,148,200,26);
        label(hwnd,"ID / Patient ID",22,178,105); gId=edit(hwnd,ID_ID,130,174,130,9);
        label(hwnd,"Name",22,209,105); gName=edit(hwnd,ID_NAME,130,205,220,100);
        label(hwnd,"Age",22,240,105); gAge=edit(hwnd,ID_AGE,130,236,70,3);
        label(hwnd,"Gender (M/F/U)",22,271,105); gGender=edit(hwnd,ID_GENDER,130,267,70,1);
        label(hwnd,"Specialization",22,302,105); gSpec=edit(hwnd,ID_SPEC,130,298,220,100);
        label(hwnd,"Doctor ID",22,333,105); gDoctor=edit(hwnd,ID_DOCTOR,130,329,100,9);
        label(hwnd,"Priority (1-5)",22,364,105); gPriority=edit(hwnd,ID_PRIORITY,130,360,100,1);
        label(hwnd,"Diagnosis",22,395,105); gDiagnosis=edit(hwnd,ID_DIAGNOSIS,130,391,220,200);
        label(hwnd,"Prescription",22,426,105); gPrescription=edit(hwnd,ID_PRESCRIPTION,130,422,220,300);
        heading(hwnd,"QUICK ACTIONS",22,465,250,25);
        button(hwnd,"Add Patient",ID_ADD_PATIENT,22,493,155); button(hwnd,"Add Doctor",ID_ADD_DOCTOR,184,493,155);
        button(hwnd,"Book Appointment",ID_BOOK,22,531,155); button(hwnd,"Add Emergency",ID_ADD_EMERGENCY,184,531,155);
        button(hwnd,"Add Medical Record",ID_ADD_RECORD,22,569,317); button(hwnd,"Clear Output",ID_CLEAR,22,607,317);
        button(hwnd,"Process Next Appointment",ID_PROCESS_APPT,22,645,155); button(hwnd,"Process Highest Emergency",ID_PROCESS_EMERGENCY,184,645,155);
        heading(hwnd,"DASHBOARD / RECORDS",375,148,300,25);
        button(hwnd,"Patients",ID_PATIENTS,375,178,125); button(hwnd,"Doctors",ID_DOCTORS,508,178,125); button(hwnd,"Appointments",ID_APPOINTMENTS,641,178,135);
        button(hwnd,"Emergency Queue",ID_EMERGENCIES,375,217,125); button(hwnd,"Medical Records",ID_RECORDS,508,217,125); button(hwnd,"Hospital Network",ID_NETWORK,641,217,135);
        button(hwnd,"BFS Traversal",ID_BFS,375,256,125); button(hwnd,"DFS Traversal",ID_DFS,508,256,125); button(hwnd,"Overview",ID_OVERVIEW,641,256,135);
        gOutput=CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","Welcome to SmartCare.\r\n\r\n1. Enter your PostgreSQL password and click Connect (or press Enter).\r\n2. Use the entry fields and action buttons.\r\n3. Use the dashboard buttons to view saved database records.\r\n\r\nThis application uses synthetic/demo data only. It is not intended for real clinical use.",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_AUTOHSCROLL|ES_READONLY|ES_WANTRETURN,375,296,760,470,hwnd,(HMENU)(INT_PTR)ID_OUTPUT,NULL,NULL);
        SendMessageA(gOutput,WM_SETFONT,(WPARAM)gMonoFont,TRUE);
        SendMessageA(gOutput,EM_SETLIMITTEXT,0,0);   // lift the ~32 KB default limit
        layoutControls(hwnd);
        return 0; }
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = 1000; mm->ptMinTrackSize.y = 740;
        return 0; }
    case WM_SIZE:
        if (IsWindow(gOutput)) layoutControls(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    case WM_COMMAND: {
        if (HIWORD(wp) != BN_CLICKED) return 0;   // ignore EN_CHANGE etc. from the edit controls
        int id=LOWORD(wp);
        if (gBusy) return 0;                      // a query is in progress
        if(id==ID_CONNECT) {
            string password=getText(gPassword);
            if(password.empty()){setOutput("Enter your PostgreSQL password first.");return 0;}
            if(GetFileAttributesA(PSQL_PATH.c_str())==INVALID_FILE_ATTRIBUTES){
                setOutput("psql.exe was not found at:\r\n"+PSQL_PATH+"\r\n\r\nInstall PostgreSQL client tools or set the 'psql' path in smartcare.ini next to this program.");
                setStatus("psql.exe not found"); return 0;
            }
            string env="PGPASSWORD="+password; _putenv(env.c_str());
            _putenv("PGCONNECT_TIMEOUT=5");
            // Wipe the password from the UI and from our own copies.
            SetWindowTextA(gPassword,"");
            for(size_t i=0;i<password.size();++i) password[i]='\0';
            for(size_t i=0;i<env.size();++i) env[i]='\0';
            gConnected=true; createSchema();
        } else if(id==ID_PATIENTS) query("SELECT patient_id, name, age, gender FROM patients ORDER BY patient_id;","PATIENT DIRECTORY");
        else if(id==ID_DOCTORS) query("SELECT doctor_id, name, specialization FROM doctors ORDER BY doctor_id;","DOCTOR DIRECTORY");
        else if(id==ID_APPOINTMENTS) query("SELECT a.appointment_id, a.patient_id, p.name AS patient, a.doctor_id, d.name AS doctor, a.status FROM appointments a LEFT JOIN patients p USING(patient_id) LEFT JOIN doctors d USING(doctor_id) ORDER BY (a.status='pending') DESC, a.appointment_id;","APPOINTMENTS (pending first)");
        else if(id==ID_EMERGENCIES) query("SELECT emergency_id, patient_id, priority, CASE priority WHEN 1 THEN 'Critical' WHEN 2 THEN 'Very Serious' WHEN 3 THEN 'Serious' WHEN 4 THEN 'Moderate' ELSE 'Low' END AS priority_name FROM emergency WHERE status='pending' ORDER BY priority, emergency_id;","EMERGENCY QUEUE (pending, highest priority first)");
        else if(id==ID_RECORDS) query("SELECT record_id, patient_id, diagnosis, prescription FROM medical_records ORDER BY record_id;","MEDICAL RECORDS");
        else if(id==ID_OVERVIEW) query("SELECT (SELECT COUNT(*) FROM patients) AS patients, (SELECT COUNT(*) FROM doctors) AS doctors, (SELECT COUNT(*) FROM appointments WHERE status='pending') AS pending_appointments, (SELECT COUNT(*) FROM emergency WHERE status='pending') AS pending_emergencies, (SELECT COUNT(*) FROM medical_records) AS medical_records;", "SMARTCARE OVERVIEW");
        else if(id==ID_PROCESS_APPT) query("WITH next_row AS (SELECT appointment_id FROM appointments WHERE status='pending' ORDER BY appointment_id LIMIT 1 FOR UPDATE SKIP LOCKED) UPDATE appointments a SET status='done' FROM next_row n WHERE a.appointment_id=n.appointment_id RETURNING a.appointment_id, a.patient_id, a.doctor_id, a.status;", "PROCESSED NEXT APPOINTMENT (FIFO; empty = nothing pending)");
        else if(id==ID_PROCESS_EMERGENCY) query("WITH next_row AS (SELECT emergency_id FROM emergency WHERE status='pending' ORDER BY priority ASC, emergency_id ASC LIMIT 1 FOR UPDATE SKIP LOCKED) UPDATE emergency e SET status='done' FROM next_row n WHERE e.emergency_id=n.emergency_id RETURNING e.emergency_id, e.patient_id, e.priority, e.status;", "PROCESSED HIGHEST-PRIORITY EMERGENCY (empty = nothing pending)");
        else if(id==ID_ADD_PATIENT) addPatient();
        else if(id==ID_ADD_DOCTOR) addDoctor();
        else if(id==ID_BOOK) bookAppointment();
        else if(id==ID_ADD_EMERGENCY) addEmergency();
        else if(id==ID_ADD_RECORD) addRecord();
        else if(id==ID_CLEAR) setOutput("");
        else if(id==ID_NETWORK) {
            string edges; buildNetwork(edges);
            setOutput("HOSPITAL NETWORK (sample graph)\r\n\r\n" + edges + "\r\nThis network is a demonstration graph and is not loaded from PostgreSQL.");
        } else if(id==ID_BFS || id==ID_DFS) {
            string edges; Graph g = buildNetwork(edges);
            if (id==ID_BFS) setOutput("BFS (Breadth-First Search) visits graph nodes level by level.\r\n\r\nTraversal from City Hospital:\r\n" + bfs(g, "City Hospital"));
            else setOutput("DFS (Depth-First Search) explores one branch before backtracking.\r\n\r\nTraversal from City Hospital:\r\n" + dfs(g, "City Hospital"));
        }
        return 0; }
    case WM_ERASEBKGND: {
        HDC dc=(HDC)wp; RECT r; GetClientRect(hwnd,&r);
        HBRUSH bg=CreateSolidBrush(C_BG); FillRect(dc,&r,bg); DeleteObject(bg);
        RECT top={0,0,r.right,76}; HBRUSH navy=CreateSolidBrush(C_NAVY); FillRect(dc,&top,navy); DeleteObject(navy);
        HBRUSH card=CreateSolidBrush(RGB(255,255,255));
        HPEN pen=CreatePen(PS_SOLID,1,RGB(226,232,242));
        HGDIOBJ oldB=SelectObject(dc,card); HGDIOBJ oldP=SelectObject(dc,pen);
        RoundRect(dc,12,82,360,(r.bottom>705?r.bottom-8:705),14,14);
        RoundRect(dc,365,130,(r.right>390?r.right-12:1180),(r.bottom>330?r.bottom-8:780),14,14);
        SelectObject(dc,oldB); SelectObject(dc,oldP); DeleteObject(card); DeleteObject(pen);
        return 1; }
    case WM_CTLCOLORDLG:
        return (LRESULT)gBackground;
    case WM_CTLCOLORSTATIC: {
        // NOTE: a read-only EDIT (the output box) also arrives here, not in WM_CTLCOLOREDIT.
        HDC dc=(HDC)wp; HWND ctl=(HWND)lp; int cid=GetDlgCtrlID(ctl);
        if(cid==ID_BRAND || cid==ID_SUBTITLE) {
            SetBkMode(dc,OPAQUE); SetBkColor(dc,C_NAVY);
            SetTextColor(dc,cid==ID_BRAND ? RGB(255,255,255) : RGB(193,211,235));
            return (LRESULT)gHeaderBrush;
        }
        SetTextColor(dc,C_TEXT);
        if(cid==ID_STATUS) {            // sits outside the white cards
            SetBkMode(dc,OPAQUE); SetBkColor(dc,C_BG);
            return (LRESULT)gBackground;
        }
        SetBkMode(dc,OPAQUE); SetBkColor(dc,RGB(255,255,255));   // labels, headings, output box
        return (LRESULT)gWhiteBrush; }
    case WM_CTLCOLOREDIT: {
        HDC dc=(HDC)wp; SetTextColor(dc,C_TEXT); SetBkColor(dc,RGB(255,255,255));
        return (LRESULT)gWhiteBrush; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d=(DRAWITEMSTRUCT*)lp;
        if(d && d->CtlType==ODT_BUTTON) {
            COLORREF fill=C_BLUE;
            int id=(int)d->CtlID;
            if(id==ID_CONNECT) fill=C_TEAL;
            else if(id==ID_ADD_PATIENT || id==ID_ADD_DOCTOR) fill=C_GREEN;
            else if(id==ID_ADD_EMERGENCY || id==ID_PROCESS_EMERGENCY) fill=C_RED;
            else if(id==ID_ADD_RECORD || id==ID_RECORDS || id==ID_DFS) fill=C_PURPLE;
            else if(id==ID_CLEAR) fill=RGB(108,122,145);
            else if(id==ID_NETWORK || id==ID_OVERVIEW) fill=C_TEAL;
            else if(id==ID_BOOK || id==ID_PROCESS_APPT) fill=C_ORANGE;
            if(d->itemState & ODS_SELECTED) { int rr=GetRValue(fill), gg=GetGValue(fill), bb=GetBValue(fill); fill=RGB(rr*4/5,gg*4/5,bb*4/5); }
            HBRUSH br=CreateSolidBrush(fill); HPEN pn=CreatePen(PS_SOLID,1,fill);
            HGDIOBJ ob=SelectObject(d->hDC,br); HGDIOBJ op=SelectObject(d->hDC,pn);
            RoundRect(d->hDC,d->rcItem.left+1,d->rcItem.top+1,d->rcItem.right-1,d->rcItem.bottom-1,9,9);
            SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,RGB(255,255,255));
            char caption[256]={0}; GetWindowTextA(d->hwndItem,caption,255);
            HGDIOBJ of=SelectObject(d->hDC,gFont);
            DrawTextA(d->hDC,caption,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            SelectObject(d->hDC,of);
            if(d->itemState & ODS_FOCUS) { RECT fr=d->rcItem; InflateRect(&fr,-5,-5); DrawFocusRect(d->hDC,&fr); }
            SelectObject(d->hDC,ob); SelectObject(d->hDC,op); DeleteObject(br); DeleteObject(pn); return TRUE;
        }
        break; }
    case WM_DESTROY:
        _putenv("PGPASSWORD=");
        if(gFont) DeleteObject(gFont);
        if(gBoldFont) DeleteObject(gBoldFont);
        if(gTitleFont) DeleteObject(gTitleFont);
        if(gMonoFont) DeleteObject(gMonoFont);
        if(gBackground) DeleteObject(gBackground);
        if(gWhiteBrush) DeleteObject(gWhiteBrush);
        if(gHeaderBrush) DeleteObject(gHeaderBrush);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show) {
    loadConfig();
    WNDCLASSA wc; ZeroMemory(&wc,sizeof(wc));
    wc.lpfnWndProc=WndProc; wc.hInstance=instance; wc.lpszClassName="SmartCareModernWindow";
    wc.hCursor=LoadCursor(NULL,IDC_ARROW); wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    wc.hIcon=LoadIcon(NULL,IDI_APPLICATION);
    if(!RegisterClassA(&wc)) return 1;
    gMain=CreateWindowExA(WS_EX_CONTROLPARENT,"SmartCareModernWindow","SmartCare | Healthcare Management",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,1420,900,NULL,NULL,instance,NULL);
    if(!gMain) return 1;
    ShowWindow(gMain,show); UpdateWindow(gMain);
    MSG msg;
    while(GetMessageA(&msg,NULL,0,0)>0){
        // Enter in the password box = Connect.
        if(msg.message==WM_KEYDOWN && msg.wParam==VK_RETURN && msg.hwnd==gPassword){
            SendMessageA(gMain,WM_COMMAND,MAKEWPARAM(ID_CONNECT,BN_CLICKED),(LPARAM)GetDlgItem(gMain,ID_CONNECT));
            continue;
        }
        // Tab / Shift+Tab / arrow-key navigation between controls.
        if(IsDialogMessageA(gMain,&msg)) continue;
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}

