// ============================================================
// SMARTCARE HEALTHCARE SYSTEM - PHASE 3
//
// Builds on phase2.cpp. Changes in Phase 3:
//   1. PostgreSQL password: "admin" by default; can be overridden
//      with the PGPASSWORD environment variable.
//   2. Database first, memory second: data is added to C++ structures
//      only after PostgreSQL confirms the insert.
//   3. Persistent queues: waiting appointments and emergencies are
//      loaded at startup; "Process" marks them DONE in PostgreSQL.
//   4. Fair emergencies: equal priorities are served first-come,
//      first-served (arrival number tie-breaker).
//   5. Safe input: numbers, ranges and gender are checked.
//   6. O(1) patient lookup through the unordered_map.
//   7. CSV loading handles quoted fields (names with commas).
//   8. Medical records use disease/treatment/medicine (fixes the
//      Phase 2 column mismatch) and the medicine field is now asked.
//   9. Cloud data exchange with Firebase (menu option 20).
//
// Build (C++11 or newer), with the cloud/ folder next to this file:
//   Dev-C++: Tools > Compiler Options > Settings > Code Generation >
//            Language standard = GNU C++11, then F9 (Compile)
//   g++ -std=gnu++11 -O2 -Wall phase3.cpp -o smartcare.exe
// Firebase (option 20) needs the variables in config/set_env.ps1.
// ============================================================

#include <iostream>
#include <string>
#include <list>
#include <queue>
#include <stack>
#include <unordered_map>
#include <vector>
#include <limits>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <cctype>

#include "cloud/cloud_menu.h"

using namespace std;

// ============================================================
// DATABASE (runs psql, same approach as Phase 2)
// ============================================================
class Database
{
private:
    string psqlPath;
    string dbName; // "smartcare" normally; set PGDATABASE=smartcare_test for load testing

    int runPSQL(const string &query, bool showOutput)
    {
        ofstream file("smartcare_temp.sql");
        if (!file)
        {
            cout << "\nCould not create temporary SQL file.\n";
            return 1;
        }
        file << query << endl;
        file.close();

        string args = " -h localhost -p 5432 -U postgres -d " + dbName + " -v ON_ERROR_STOP=1 ";
        if (!showOutput)
            args += "-q ";
        args += "-f \"smartcare_temp.sql\"";

#ifdef _WIN32
        string command = "cmd /c \"\"" + psqlPath + "\"" + args + "\"";
#else
        string command = "\"" + psqlPath + "\"" + args;
#endif
        int result = system(command.c_str());
        remove("smartcare_temp.sql");
        return result;
    }

public:
    Database()
    {
        psqlPath = smartcare::readEnv("SMARTCARE_PSQL");
        if (psqlPath.empty())
            psqlPath = "C:\\Program Files\\PostgreSQL\\18\\bin\\psql.exe";

        // PostgreSQL password: uses PGPASSWORD if set, otherwise "admin".
        if (smartcare::readEnv("PGPASSWORD").empty())
        {
#ifdef _WIN32
            _putenv("PGPASSWORD=admin");
#else
            setenv("PGPASSWORD", "admin", 1);
#endif
        }

        dbName = smartcare::readEnv("PGDATABASE");
        if (dbName.empty())
            dbName = "smartcare";
        for (size_t i = 0; i < dbName.size(); i++) // used on a command line: letters, digits, _ only
            if (!(isalnum((unsigned char)dbName[i]) || dbName[i] == '_'))
            {
                cout << "\nInvalid PGDATABASE name; using smartcare.\n";
                dbName = "smartcare";
                break;
            }

        cout << "\nConnecting to PostgreSQL database '" << dbName << "'...\n";
        if (runPSQL("SELECT 1;", false) == 0)
            cout << "PostgreSQL connected successfully!\n";
        else
        {
            cout << "PostgreSQL connection failed!\n";
            cout << "Check PostgreSQL service, database name, username and PGPASSWORD.\n";
        }
    }

    ~Database() { remove("smartcare_temp.sql"); }

    bool isConnected() { return runPSQL("SELECT 1;", false) == 0; }
    bool execute(string query) { return runPSQL(query, false) == 0; }
    void showQuery(string query) { runPSQL(query, true); }

    // Runs a SELECT and saves the result to a CSV file (no header).
    bool exportToCsv(const string &selectQuery, const string &fileName)
    {
        return execute("\\copy (" + selectQuery + ") TO '" + fileName +
                       "' WITH (FORMAT csv, HEADER false)");
    }
};

// ============================================================
// PATIENT CLASS
// ============================================================
class Patient
{
public:
    int id;
    string name;
    int age;
    char gender;

    Patient(int i, string n, int a, char g)
    {
        id = i;
        name = n;
        age = a;
        gender = g;
    }

    void display() const
    {
        cout << "ID: " << id << " | Name: " << name << " | Age: " << age
             << " | Gender: " << gender << endl;
    }
};

// ============================================================
// DOCTOR CLASS
// ============================================================
class Doctor
{
public:
    int id;
    string name;
    string specialization;

    Doctor(int i, string n, string s)
    {
        id = i;
        name = n;
        specialization = s;
    }

    void display() const
    {
        cout << "ID: " << id << " | Doctor: " << name
             << " | Specialization: " << specialization << endl;
    }
};

// ============================================================
// APPOINTMENT CLASS (now remembers its database ID)
// ============================================================
class Appointment
{
public:
    int appointmentID;
    int patientID;
    int doctorID;

    Appointment(int a, int p, int d)
    {
        appointmentID = a;
        patientID = p;
        doctorID = d;
    }

    void display() const
    {
        cout << "Appointment #" << appointmentID << " | Patient ID: " << patientID
             << " | Doctor ID: " << doctorID << endl;
    }
};

// ============================================================
// EMERGENCY CLASS
// seq = emergency_id from PostgreSQL = arrival order
// ============================================================
class Emergency
{
public:
    int patientID;
    int priority;
    long long seq;

    Emergency(int p, int pr, long long s)
    {
        patientID = p;
        priority = pr;
        seq = s;
    }

    void display() const
    {
        cout << "Case #" << seq << " | Patient ID: " << patientID
             << " | Priority: " << priority << " | " << getPriorityName() << endl;
    }

    string getPriorityName() const
    {
        if (priority == 1) return "Critical";
        else if (priority == 2) return "Very Serious";
        else if (priority == 3) return "Serious";
        else if (priority == 4) return "Moderate";
        else return "Low";
    }
};

// ============================================================
// EMERGENCY COMPARATOR (Min-Heap)
// Lower priority number = more urgent.
// Same priority -> whoever arrived first (smaller seq) goes first.
// Matches SQL: ORDER BY priority, emergency_id
// ============================================================
struct CompareEmergency
{
    bool operator()(const Emergency &a, const Emergency &b) const
    {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.seq > b.seq;
    }
};

typedef priority_queue<Emergency, vector<Emergency>, CompareEmergency> EmergencyQueue;

// ============================================================
// HOSPITAL GRAPH (BFS / DFS)
// ============================================================
class HospitalNetwork
{
private:
    unordered_map<string, vector<string> > graph;

public:
    void addConnection(string hospital1, string hospital2)
    {
        graph[hospital1].push_back(hospital2);
        graph[hospital2].push_back(hospital1);
    }

    void BFS(string start)
    {
        if (graph.find(start) == graph.end())
        {
            cout << "Hospital not found.\n";
            return;
        }
        queue<string> q;
        unordered_map<string, bool> visited;
        q.push(start);
        visited[start] = true;

        cout << "\nBFS Hospital Traversal:\n";
        while (!q.empty())
        {
            string current = q.front();
            q.pop();
            cout << current << " -> ";
            for (size_t i = 0; i < graph[current].size(); i++)
            {
                string next = graph[current][i];
                if (!visited[next])
                {
                    visited[next] = true;
                    q.push(next);
                }
            }
        }
        cout << "END\n";
    }

    void DFS(string start)
    {
        if (graph.find(start) == graph.end())
        {
            cout << "Hospital not found.\n";
            return;
        }
        stack<string> s;
        unordered_map<string, bool> visited;
        s.push(start);

        cout << "\nDFS Hospital Traversal:\n";
        while (!s.empty())
        {
            string current = s.top();
            s.pop();
            if (visited[current])
                continue;
            visited[current] = true;
            cout << current << " -> ";
            for (int i = (int)graph[current].size() - 1; i >= 0; i--)
            {
                string next = graph[current][i];
                if (!visited[next])
                    s.push(next);
            }
        }
        cout << "END\n";
    }

    void displayNetwork()
    {
        cout << "\n========== HOSPITAL NETWORK ==========\n";
        for (unordered_map<string, vector<string> >::iterator it = graph.begin();
             it != graph.end(); ++it)
        {
            cout << it->first << " : ";
            for (size_t i = 0; i < it->second.size(); i++)
                cout << it->second[i] << " | ";
            cout << endl;
        }
    }
};

// ============================================================
// INPUT HELPERS (line-based, never leave cin in a failed state)
// ============================================================
string readLine(const string &prompt)
{
    cout << prompt;
    string line;
    if (!getline(cin, line))
    {
        cout << "\nInput closed. Exiting.\n";
        exit(0);
    }
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    return line;
}

string trim(const string &s)
{
    size_t a = s.find_first_not_of(" \t");
    if (a == string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

bool parseInt(const string &text, int &out)
{
    string t = trim(text);
    if (t.empty() || t.size() > 9)
        return false;
    size_t start = (t[0] == '-') ? 1 : 0;
    if (start == t.size())
        return false;
    for (size_t i = start; i < t.size(); i++)
        if (!isdigit((unsigned char)t[i]))
            return false;
    out = atoi(t.c_str());
    return true;
}

int readInt(const string &prompt, int lo, int hi)
{
    while (true)
    {
        int v;
        if (parseInt(readLine(prompt), v) && v >= lo && v <= hi)
            return v;
        cout << "Enter a whole number between " << lo << " and " << hi << ".\n";
    }
}

string readText(const string &prompt, size_t maxLen)
{
    while (true)
    {
        string s = trim(readLine(prompt));
        if (s.empty())
            cout << "This field cannot be empty.\n";
        else if (s.size() > maxLen)
            cout << "Too long (max " << maxLen << " characters).\n";
        else
            return s;
    }
}

char readGender()
{
    while (true)
    {
        string g = trim(readLine("Enter Gender (M/F/O): "));
        if (g.size() == 1)
        {
            char c = (char)toupper((unsigned char)g[0]);
            if (c == 'M' || c == 'F' || c == 'O')
                return c;
        }
        cout << "Please enter M, F or O.\n";
    }
}

void pauseScreen()
{
    readLine("\nPress Enter to continue...");
}

void displayHeader()
{
    cout << "\n================================================\n";
    cout << "        SMARTCARE HEALTHCARE SYSTEM - PHASE 3\n";
    cout << "================================================\n";
}

string escapeSQL(const string &text)
{
    string result;
    for (size_t i = 0; i < text.length(); i++)
    {
        if (text[i] == '\'')
            result += "''";
        else
            result += text[i];
    }
    return result;
}

// Splits one CSV line, handling "quoted, fields" and "" escapes.
vector<string> parseCsvLine(const string &line)
{
    vector<string> fields;
    string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); i++)
    {
        char c = line[i];
        if (inQuotes)
        {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"')
            {
                cur += '"';
                i++;
            }
            else if (c == '"')
                inQuotes = false;
            else
                cur += c;
        }
        else if (c == '"')
            inQuotes = true;
        else if (c == ',')
        {
            fields.push_back(cur);
            cur.clear();
        }
        else if (c != '\r')
            cur += c;
    }
    fields.push_back(cur);
    return fields;
}

// Reads every row of a CSV file exported by Database::exportToCsv.
vector<vector<string> > readCsvFile(const string &fileName)
{
    vector<vector<string> > rows;
    ifstream file(fileName.c_str());
    string line;
    while (getline(file, line))
        if (!line.empty())
            rows.push_back(parseCsvLine(line));
    file.close();
    remove(fileName.c_str());
    return rows;
}

// ============================================================
// CREATE / UPGRADE DATABASE TABLES
// (constraints and foreign keys: run database/phase3_migration.sql)
// ============================================================
void createTables(Database &db)
{
    db.execute(
        "CREATE TABLE IF NOT EXISTS patients ("
        "patient_id INTEGER PRIMARY KEY,"
        "name VARCHAR(100) NOT NULL,"
        "age INTEGER,"
        "gender CHAR(1));"
        "CREATE TABLE IF NOT EXISTS doctors ("
        "doctor_id INTEGER PRIMARY KEY,"
        "name VARCHAR(100) NOT NULL,"
        "specialization VARCHAR(100));"
        "CREATE TABLE IF NOT EXISTS appointments ("
        "appointment_id SERIAL PRIMARY KEY,"
        "patient_id INTEGER,"
        "doctor_id INTEGER);"
        "CREATE TABLE IF NOT EXISTS emergency ("
        "emergency_id SERIAL PRIMARY KEY,"
        "patient_id INTEGER,"
        "priority INTEGER);"
        "CREATE TABLE IF NOT EXISTS medical_records ("
        "record_id SERIAL PRIMARY KEY,"
        "patient_id INTEGER,"
        "disease VARCHAR(200),"
        "treatment VARCHAR(300),"
        "medicine VARCHAR(300));"
        // Phase 2 created this table with diagnosis/prescription but inserted
        // into disease/treatment/medicine. Make sure the used columns exist.
        "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS disease VARCHAR(200);"
        "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS treatment VARCHAR(300);"
        "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS medicine VARCHAR(300);"
        // Phase 3 columns needed by this program
        "ALTER TABLE patients ADD COLUMN IF NOT EXISTS source VARCHAR(40) NOT NULL DEFAULT 'LOCAL';"
        "ALTER TABLE appointments ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'WAITING';"
        "ALTER TABLE emergency ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'WAITING';"
        "CREATE TABLE IF NOT EXISTS cloud_import_log ("
        "log_id SERIAL PRIMARY KEY,"
        "imported_at TIMESTAMPTZ NOT NULL DEFAULT now(),"
        "cloud_path VARCHAR(200) NOT NULL,"
        "record_key VARCHAR(60) NOT NULL,"
        "patient_id INTEGER,"
        "status VARCHAR(10) NOT NULL CHECK (status IN ('IMPORTED','SKIPPED','REJECTED')),"
        "reason VARCHAR(200));");

    cout << "\nDatabase tables are ready.\n";
}

// ============================================================
// LOOKUPS
// ============================================================
bool patientExists(const unordered_map<int, Patient> &patientHash, int id)
{
    return patientHash.count(id) > 0; // O(1) average
}

bool doctorExists(const vector<Doctor> &doctors, int id)
{
    for (size_t i = 0; i < doctors.size(); i++)
        if (doctors[i].id == id)
            return true;
    return false;
}

// ============================================================
// LOAD DATA FROM POSTGRESQL INTO C++ STRUCTURES
// ============================================================
void loadPatients(Database &db, list<Patient> &patients, unordered_map<int, Patient> &patientHash)
{
    if (!db.exportToCsv("SELECT patient_id, name, age, gender FROM patients ORDER BY patient_id",
                        "smartcare_patients.csv"))
        return;
    vector<vector<string> > rows = readCsvFile("smartcare_patients.csv");
    for (size_t i = 0; i < rows.size(); i++)
    {
        if (rows[i].size() < 4)
            continue;
        int id, age = 0;
        if (!parseInt(rows[i][0], id))
            continue;
        parseInt(rows[i][2], age);
        char gender = rows[i][3].empty() ? 'U' : rows[i][3][0];
        if (!patientExists(patientHash, id))
        {
            Patient p(id, rows[i][1], age, gender);
            patients.push_back(p);
            patientHash.emplace(id, p);
        }
    }
}

void loadDoctors(Database &db, vector<Doctor> &doctors)
{
    if (!db.exportToCsv("SELECT doctor_id, name, specialization FROM doctors ORDER BY doctor_id",
                        "smartcare_doctors.csv"))
        return;
    vector<vector<string> > rows = readCsvFile("smartcare_doctors.csv");
    for (size_t i = 0; i < rows.size(); i++)
    {
        int id;
        if (rows[i].size() < 3 || !parseInt(rows[i][0], id))
            continue;
        if (!doctorExists(doctors, id))
            doctors.push_back(Doctor(id, rows[i][1], rows[i][2]));
    }
}

// Rebuilds the FIFO queue from waiting appointments (oldest first).
void loadAppointments(Database &db, queue<Appointment> &appointments)
{
    queue<Appointment> fresh;
    if (db.exportToCsv("SELECT appointment_id, patient_id, doctor_id FROM appointments "
                       "WHERE status = 'WAITING' ORDER BY appointment_id",
                       "smartcare_appointments.csv"))
    {
        vector<vector<string> > rows = readCsvFile("smartcare_appointments.csv");
        for (size_t i = 0; i < rows.size(); i++)
        {
            int a, p, d;
            if (rows[i].size() >= 3 && parseInt(rows[i][0], a) &&
                parseInt(rows[i][1], p) && parseInt(rows[i][2], d))
                fresh.push(Appointment(a, p, d));
        }
    }
    appointments.swap(fresh);
}

// Rebuilds the min-heap from waiting emergency cases.
void loadEmergencies(Database &db, EmergencyQueue &emergencies)
{
    EmergencyQueue fresh;
    if (db.exportToCsv("SELECT emergency_id, patient_id, priority FROM emergency "
                       "WHERE status = 'WAITING' ORDER BY priority, emergency_id",
                       "smartcare_emergency.csv"))
    {
        vector<vector<string> > rows = readCsvFile("smartcare_emergency.csv");
        for (size_t i = 0; i < rows.size(); i++)
        {
            int e, p, pr;
            if (rows[i].size() >= 3 && parseInt(rows[i][0], e) &&
                parseInt(rows[i][1], p) && parseInt(rows[i][2], pr))
                fresh.push(Emergency(p, pr, e));
        }
    }
    emergencies.swap(fresh);
}

// ============================================================
// MAIN
// ============================================================
int main()
{
    Database db;
    if (!db.isConnected())
    {
        cout << "\nPlease check that PostgreSQL is running and the password is correct.\n";
        return 0;
    }
    createTables(db);

    // --------------------------------------------------------
    // DATA STRUCTURES
    // --------------------------------------------------------
    list<Patient> patients;                  // Linked List
    unordered_map<int, Patient> patientHash; // Hash Table
    vector<Doctor> doctors;                  // Vector
    queue<Appointment> appointments;         // Queue (FIFO)
    EmergencyQueue emergencies;              // Priority Queue (Min-Heap)
    stack<string> actions;                   // Stack (LIFO)

    loadPatients(db, patients, patientHash);
    loadDoctors(db, doctors);
    loadAppointments(db, appointments);
    loadEmergencies(db, emergencies);

    cout << "\nExisting database data loaded into C++.";
    cout << "\nPatients loaded: " << patients.size();
    cout << "\nDoctors loaded: " << doctors.size();
    cout << "\nWaiting appointments: " << appointments.size();
    cout << "\nWaiting emergencies: " << emergencies.size() << "\n";

    // --------------------------------------------------------
    // HOSPITAL NETWORK (Graph)
    // --------------------------------------------------------
    HospitalNetwork hospitalNetwork;
    hospitalNetwork.addConnection("City Hospital", "Apollo Hospital");
    hospitalNetwork.addConnection("Apollo Hospital", "Fortis Hospital");
    hospitalNetwork.addConnection("Fortis Hospital", "Max Hospital");
    hospitalNetwork.addConnection("City Hospital", "AIIMS");

    // --------------------------------------------------------
    // CLOUD CLIENT (reads settings from environment variables)
    // --------------------------------------------------------
    smartcare::FirebaseClient firebase = smartcare::FirebaseClient::fromEnvironment();

    int choice;
    do
    {
        displayHeader();
        cout << "\n1. Add Patient";
        cout << "\n2. Display Patients";
        cout << "\n3. Search Patient";
        cout << "\n4. Add Doctor";
        cout << "\n5. Display Doctors";
        cout << "\n6. Book Appointment";
        cout << "\n7. Process Appointment";
        cout << "\n8. Add Emergency Case";
        cout << "\n9. Process Emergency Case";
        cout << "\n10. Add Medical Record";
        cout << "\n11. View Medical Records";
        cout << "\n12. View Database Patients";
        cout << "\n13. View Database Doctors";
        cout << "\n14. View Database Appointments";
        cout << "\n15. View Database Emergency";
        cout << "\n16. Hospital Network";
        cout << "\n17. BFS Hospital Traversal";
        cout << "\n18. DFS Hospital Traversal";
        cout << "\n19. Recent Actions";
        cout << "\n20. Cloud Data Exchange (Firebase)";
        cout << "\n0. Exit\n";

        choice = readInt("\nEnter your choice: ", 0, 20);

        // ====================================================
        // ADD PATIENT
        // ====================================================
        if (choice == 1)
        {
            int id = readInt("\nEnter Patient ID (1-99999): ", 1, 99999);
            if (patientExists(patientHash, id))
                cout << "Patient already exists.\n";
            else
            {
                string name = readText("Enter Patient Name: ", 100);
                int age = readInt("Enter Age (0-120): ", 0, 120);
                char gender = readGender();

                string query = "INSERT INTO patients (patient_id, name, age, gender) VALUES (" +
                               to_string(id) + ", '" + escapeSQL(name) + "', " +
                               to_string(age) + ", '" + string(1, gender) + "')";

                // Database first, then memory
                if (db.execute(query))
                {
                    Patient p(id, name, age, gender);
                    patients.push_back(p);
                    patientHash.emplace(id, p);
                    cout << "\nPatient added and stored in PostgreSQL!\n";
                    actions.push("Added Patient: " + name);
                }
                else
                    cout << "\nCould not save patient to PostgreSQL. Nothing was added.\n";
            }
        }

        // ====================================================
        // DISPLAY PATIENTS
        // ====================================================
        else if (choice == 2)
        {
            cout << "\n========== PATIENTS ==========\n";
            if (patients.empty())
                cout << "No patients available.\n";
            else
                for (list<Patient>::iterator it = patients.begin(); it != patients.end(); ++it)
                    it->display();
        }

        // ====================================================
        // SEARCH PATIENT (Hash Table, O(1) average)
        // ====================================================
        else if (choice == 3)
        {
            int id = readInt("\nEnter Patient ID to search: ", 1, 999999);
            unordered_map<int, Patient>::iterator it = patientHash.find(id);
            if (it != patientHash.end())
            {
                cout << "\nPatient Found:\n";
                it->second.display();
            }
            else
                cout << "\nPatient not found.\n";
        }

        // ====================================================
        // ADD DOCTOR
        // ====================================================
        else if (choice == 4)
        {
            int id = readInt("\nEnter Doctor ID (1-99999): ", 1, 99999);
            if (doctorExists(doctors, id))
                cout << "Doctor already exists.\n";
            else
            {
                string name = readText("Enter Doctor Name: ", 100);
                string specialization = readText("Enter Specialization: ", 100);

                string query = "INSERT INTO doctors (doctor_id, name, specialization) VALUES (" +
                               to_string(id) + ", '" + escapeSQL(name) + "', '" +
                               escapeSQL(specialization) + "')";

                if (db.execute(query))
                {
                    doctors.push_back(Doctor(id, name, specialization));
                    cout << "\nDoctor added and stored in PostgreSQL!\n";
                    actions.push("Added Doctor: " + name);
                }
                else
                    cout << "\nCould not save doctor to PostgreSQL. Nothing was added.\n";
            }
        }

        // ====================================================
        // DISPLAY DOCTORS
        // ====================================================
        else if (choice == 5)
        {
            cout << "\n========== DOCTORS ==========\n";
            if (doctors.empty())
                cout << "No doctors available.\n";
            else
                for (size_t i = 0; i < doctors.size(); i++)
                    doctors[i].display();
        }

        // ====================================================
        // BOOK APPOINTMENT (Queue)
        // ====================================================
        else if (choice == 6)
        {
            int patientID = readInt("\nEnter Patient ID: ", 1, 999999);
            if (!patientExists(patientHash, patientID))
                cout << "Patient does not exist.\n";
            else
            {
                int doctorID = readInt("Enter Doctor ID: ", 1, 99999);
                if (!doctorExists(doctors, doctorID))
                    cout << "Doctor does not exist.\n";
                else if (db.execute("INSERT INTO appointments (patient_id, doctor_id, status) VALUES (" +
                                    to_string(patientID) + ", " + to_string(doctorID) + ", 'WAITING')"))
                {
                    // Reload so the queue holds the real appointment IDs from PostgreSQL
                    loadAppointments(db, appointments);
                    cout << "\nAppointment booked and stored in PostgreSQL!";
                    cout << "\nPosition in queue: " << appointments.size() << "\n";
                    actions.push("Booked Appointment for Patient " + to_string(patientID));
                }
                else
                    cout << "\nCould not save appointment.\n";
            }
        }

        // ====================================================
        // PROCESS APPOINTMENT (FIFO)
        // ====================================================
        else if (choice == 7)
        {
            if (appointments.empty())
                cout << "\nNo appointments waiting.\n";
            else
            {
                Appointment a = appointments.front();
                if (db.execute("UPDATE appointments SET status = 'DONE' WHERE appointment_id = " +
                               to_string(a.appointmentID)))
                {
                    appointments.pop();
                    cout << "\nProcessing Appointment:\n";
                    a.display();
                    cout << "Marked DONE in PostgreSQL.\n";
                    actions.push("Processed Appointment #" + to_string(a.appointmentID));
                }
                else
                    cout << "\nCould not update PostgreSQL. Appointment kept in queue.\n";
            }
        }

        // ====================================================
        // ADD EMERGENCY (Priority Queue)
        // ====================================================
        else if (choice == 8)
        {
            int patientID = readInt("\nEnter Patient ID: ", 1, 999999);
            if (!patientExists(patientHash, patientID))
                cout << "Patient does not exist.\n";
            else
            {
                cout << "Priority levels: 1-Critical 2-Very Serious 3-Serious 4-Moderate 5-Low\n";
                int priority = readInt("Enter Priority (1-5): ", 1, 5);

                if (db.execute("INSERT INTO emergency (patient_id, priority, status) VALUES (" +
                               to_string(patientID) + ", " + to_string(priority) + ", 'WAITING')"))
                {
                    loadEmergencies(db, emergencies); // real emergency_id = arrival order
                    cout << "\nEmergency case added and stored in PostgreSQL!\n";
                    actions.push("Added Emergency for Patient " + to_string(patientID));
                }
                else
                    cout << "\nCould not save emergency case.\n";
            }
        }

        // ====================================================
        // PROCESS EMERGENCY (most urgent first)
        // ====================================================
        else if (choice == 9)
        {
            if (emergencies.empty())
                cout << "\nNo emergency cases waiting.\n";
            else
            {
                Emergency e = emergencies.top();
                if (db.execute("UPDATE emergency SET status = 'DONE' WHERE emergency_id = " +
                               to_string(e.seq)))
                {
                    emergencies.pop();
                    cout << "\nProcessing Emergency Case:\n";
                    e.display();
                    cout << "Marked DONE in PostgreSQL. Cases still waiting: "
                         << emergencies.size() << "\n";
                    actions.push("Processed Emergency #" + to_string(e.seq));
                }
                else
                    cout << "\nCould not update PostgreSQL. Case kept in queue.\n";
            }
        }

        // ====================================================
        // ADD MEDICAL RECORD
        // ====================================================
        else if (choice == 10)
        {
            int patientID = readInt("\nEnter Patient ID: ", 1, 999999);
            if (!patientExists(patientHash, patientID))
                cout << "Patient does not exist.\n";
            else
            {
                string disease = readText("Enter Disease: ", 200);
                string treatment = readText("Enter Treatment: ", 300);
                string medicine = trim(readLine("Enter Medicine (optional, press Enter to skip): "));
                if (medicine.size() > 300)
                    medicine = medicine.substr(0, 300);

                if (db.execute("INSERT INTO medical_records (patient_id, disease, treatment, medicine) VALUES (" +
                               to_string(patientID) + ", '" + escapeSQL(disease) + "', '" +
                               escapeSQL(treatment) + "', '" + escapeSQL(medicine) + "')"))
                {
                    cout << "\nMedical record stored in PostgreSQL!\n";
                    actions.push("Added Medical Record for Patient " + to_string(patientID));
                }
                else
                    cout << "\nCould not save medical record.\n";
            }
        }

        // ====================================================
        // VIEW MEDICAL RECORDS
        // ====================================================
        else if (choice == 11)
        {
            int patientID = readInt("\nEnter Patient ID (0 = all records): ", 0, 999999);
            cout << "\n========== MEDICAL RECORDS ==========\n";
            if (patientID == 0)
                db.showQuery("SELECT m.record_id, m.patient_id, p.name, m.disease, m.treatment, m.medicine "
                             "FROM medical_records m LEFT JOIN patients p USING (patient_id) "
                             "ORDER BY m.record_id;");
            else
                db.showQuery("SELECT record_id, disease, treatment, medicine FROM medical_records "
                             "WHERE patient_id = " + to_string(patientID) + " ORDER BY record_id;");
        }

        // ====================================================
        // VIEW DATABASE TABLES
        // ====================================================
        else if (choice == 12)
            db.showQuery("SELECT patient_id, name, age, gender, source FROM patients ORDER BY patient_id;");
        else if (choice == 13)
            db.showQuery("SELECT * FROM doctors ORDER BY doctor_id;");
        else if (choice == 14)
            db.showQuery("SELECT appointment_id, patient_id, doctor_id, status FROM appointments "
                         "ORDER BY appointment_id;");
        else if (choice == 15)
            db.showQuery("SELECT emergency_id, patient_id, priority, status FROM emergency "
                         "ORDER BY status DESC, priority, emergency_id;");

        // ====================================================
        // HOSPITAL NETWORK (Graph, BFS, DFS)
        // ====================================================
        else if (choice == 16)
            hospitalNetwork.displayNetwork();
        else if (choice == 17)
            hospitalNetwork.BFS(trim(readLine("\nEnter starting hospital: ")));
        else if (choice == 18)
            hospitalNetwork.DFS(trim(readLine("\nEnter starting hospital: ")));

        // ====================================================
        // RECENT ACTIONS (Stack, newest first)
        // ====================================================
        else if (choice == 19)
        {
            cout << "\n========== RECENT ACTIONS ==========\n";
            if (actions.empty())
                cout << "No actions yet.\n";
            else
            {
                stack<string> copy = actions; // keep the original stack intact
                int shown = 0;
                while (!copy.empty() && shown < 10)
                {
                    cout << ++shown << ". " << copy.top() << endl;
                    copy.pop();
                }
            }
        }

        // ====================================================
        // CLOUD DATA EXCHANGE (Firebase)
        // ====================================================
        else if (choice == 20)
        {
            smartcare::cloudMenu(firebase, db, patients, patientHash, actions);
            cin.ignore(numeric_limits<streamsize>::max(), '\n'); // finish the cloud menu's last line
        }

        if (choice != 0 && choice != 20)
            pauseScreen();

    } while (choice != 0);

    cout << "\nThank you for using SmartCare!\n";
    return 0;
}
