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

using namespace std;

class Database
{
private:
    string psqlPath;

    int runPSQL(string query, bool showOutput)
    {
        ofstream file("smartcare_temp.sql");

        if (!file)
        {
            cout << "\nCould not create temporary SQL file.\n";
            return 1;
        }

        file << query << endl;
        file.close();

        string command =
            "cmd /c \"\"C:\\Program Files\\PostgreSQL\\18\\bin\\psql.exe\" "
            "-h localhost "
            "-p 5432 "
            "-U postgres "
            "-d smartcare "
            "-v ON_ERROR_STOP=1 ";

        if (!showOutput)
            command += "-q ";

        command += "-f \"smartcare_temp.sql\"\"";

        int result = system(command.c_str());

        remove("smartcare_temp.sql");

        return result;
    }

public:

    Database()
    {
        psqlPath =
            "C:\\Program Files\\PostgreSQL\\18\\bin\\psql.exe";

  
        _putenv("PGPASSWORD=admin");

        cout << "\nConnecting to PostgreSQL...\n";

        if (runPSQL("SELECT 1;", false) == 0)
        {
            cout << "PostgreSQL connected successfully!\n";
        }
        else
        {
            cout << "PostgreSQL connection failed!\n";
            cout << "Check PostgreSQL service, database name, "
                 << "username and password.\n";
        }
    }

    ~Database()
    {
        remove("smartcare_temp.sql");
        _putenv("PGPASSWORD=");
    }

    bool isConnected()
    {
        return runPSQL("SELECT 1;", false) == 0;
    }

    bool execute(string query)
    {
        return runPSQL(query, false) == 0;
    }

    void showQuery(string query)
    {
        runPSQL(query, true);
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
        cout << "ID: " << id
             << " | Name: " << name
             << " | Age: " << age
             << " | Gender: " << gender
             << endl;
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
        cout << "ID: " << id
             << " | Doctor: " << name
             << " | Specialization: "
             << specialization << endl;
    }
};


// ============================================================
// APPOINTMENT CLASS
// ============================================================

class Appointment
{
public:
    int patientID;
    int doctorID;

    Appointment(int p, int d)
    {
        patientID = p;
        doctorID = d;
    }

    void display() const
    {
        cout << "Patient ID: " << patientID
             << " | Doctor ID: " << doctorID
             << endl;
    }
};


// ============================================================
// EMERGENCY CLASS
// ============================================================

class Emergency
{
public:
    int patientID;
    int priority;

    Emergency(int p, int pr)
    {
        patientID = p;
        priority = pr;
    }

    void display() const
    {
        cout << "Patient ID: " << patientID
             << " | Priority: " << priority
             << " | " << getPriorityName()
             << endl;
    }

    string getPriorityName() const
    {
        if (priority == 1)
            return "Critical";
        else if (priority == 2)
            return "Very Serious";
        else if (priority == 3)
            return "Serious";
        else if (priority == 4)
            return "Moderate";
        else
            return "Low";
    }
};


// ============================================================
// EMERGENCY COMPARATOR
// Lower priority number = Higher priority
// ============================================================

struct CompareEmergency
{
    bool operator()(const Emergency &a,
                    const Emergency &b) const
    {
        return a.priority > b.priority;
    }
};


// ============================================================
// HOSPITAL GRAPH
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

            for (size_t i = 0;
                 i < graph[current].size();
                 i++)
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

            for (int i = (int)graph[current].size() - 1;
                 i >= 0;
                 i--)
            {
                string next = graph[current][i];

                if (!visited[next])
                {
                    s.push(next);
                }
            }
        }

        cout << "END\n";
    }

    void displayNetwork()
    {
        cout << "\n========== HOSPITAL NETWORK ==========\n";

        for (unordered_map<string,
             vector<string> >::iterator it = graph.begin();
             it != graph.end();
             ++it)
        {
            cout << it->first << " : ";

            for (size_t i = 0;
                 i < it->second.size();
                 i++)
            {
                cout << it->second[i] << " ";
            }

            cout << endl;
        }
    }
};


// ============================================================
// INPUT FUNCTIONS
// ============================================================

void clearInput()
{
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}


void pauseScreen()
{
    cout << "\nPress Enter to continue...";
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cin.get();
}


void displayHeader()
{
    cout << "\n================================================\n";
    cout << "       SMARTCARE HEALTHCARE SYSTEM\n";
    cout << "================================================\n";
}


string escapeSQL(string text)
{
    string result = "";

    for (size_t i = 0; i < text.length(); i++)
    {
        if (text[i] == '\'')
            result += "''";
        else
            result += text[i];
    }

    return result;
}


// ============================================================
// CREATE DATABASE TABLES
// ============================================================

void createTables(Database &db)
{
    db.execute(
        "CREATE TABLE IF NOT EXISTS patients ("
        "patient_id INTEGER PRIMARY KEY,"
        "name VARCHAR(100) NOT NULL,"
        "age INTEGER,"
        "gender CHAR(1)"
        ")"
    );

    db.execute(
        "CREATE TABLE IF NOT EXISTS doctors ("
        "doctor_id INTEGER PRIMARY KEY,"
        "name VARCHAR(100) NOT NULL,"
        "specialization VARCHAR(100)"
        ")"
    );

    db.execute(
        "CREATE TABLE IF NOT EXISTS appointments ("
        "appointment_id SERIAL PRIMARY KEY,"
        "patient_id INTEGER,"
        "doctor_id INTEGER"
        ")"
    );

    db.execute(
        "CREATE TABLE IF NOT EXISTS emergency ("
        "emergency_id SERIAL PRIMARY KEY,"
        "patient_id INTEGER,"
        "priority INTEGER"
        ")"
    );

    db.execute(
        "CREATE TABLE IF NOT EXISTS medical_records ("
        "record_id SERIAL PRIMARY KEY,"
        "patient_id INTEGER,"
        "diagnosis VARCHAR(200),"
        "prescription VARCHAR(300)"
        ")"
    );

    cout << "\nDatabase tables are ready.\n";
}


// ============================================================
// CHECK DOCTOR
// ============================================================

bool doctorExists(vector<Doctor> &doctors, int id)
{
    for (size_t i = 0; i < doctors.size(); i++)
    {
        if (doctors[i].id == id)
            return true;
    }

    return false;
}


// ============================================================
// CHECK PATIENT
// ============================================================

bool patientExists(list<Patient> &patients, int id)
{
    for (list<Patient>::iterator it = patients.begin();
         it != patients.end();
         ++it)
    {
        if (it->id == id)
            return true;
    }

    return false;
}


bool doctorExists(vector<Doctor> &doctors, int id);
bool patientExists(list<Patient> &patients, int id);

// ============================================================
// LOAD EXISTING DATA FROM POSTGRESQL
// ============================================================

void loadPatientsFromDatabase(Database &db,
                              list<Patient> &patients,
                              unordered_map<int, Patient> &patientHash)
{
    // psql output is read through a temporary CSV file.
    string query =
        "\\copy (SELECT patient_id, name, age, gender "
        "FROM patients ORDER BY patient_id) TO 'smartcare_patients.csv' "
        "WITH (FORMAT csv, HEADER false)";

    if (!db.execute(query))
        return;

    ifstream file("smartcare_patients.csv");

    if (!file)
        return;

    string line;

    while (getline(file, line))
    {
        if (line.empty())
            continue;

        size_t p1 = line.find(',');
        size_t p2 = line.find(',', p1 + 1);
        size_t p3 = line.find(',', p2 + 1);

        if (p1 == string::npos || p2 == string::npos || p3 == string::npos)
            continue;

        string idText = line.substr(0, p1);
        string name = line.substr(p1 + 1, p2 - p1 - 1);
        string ageText = line.substr(p2 + 1, p3 - p2 - 1);
        string genderText = line.substr(p3 + 1);

        // Remove CSV quotes around simple fields if present.
        if (name.size() >= 2 && name[0] == '"' &&
            name[name.size() - 1] == '"')
        {
            name = name.substr(1, name.size() - 2);
        }

        if (genderText.size() >= 2 && genderText[0] == '"' &&
            genderText[genderText.size() - 1] == '"')
        {
            genderText = genderText.substr(1, genderText.size() - 2);
        }

        try
        {
            int id = atoi(idText.c_str());
            int age = atoi(ageText.c_str());
            char gender = genderText.empty() ? 'U' : genderText[0];

            Patient p(id, name, age, gender);

            if (!patientExists(patients, id))
            {
                patients.push_back(p);
                patientHash.emplace(id, p);
            }
        }
        catch (...)
        {
            // Ignore malformed rows.
        }
    }

    file.close();
    remove("smartcare_patients.csv");
}


void loadDoctorsFromDatabase(Database &db,
                             vector<Doctor> &doctors)
{
    string query =
        "\\copy (SELECT doctor_id, name, specialization "
        "FROM doctors ORDER BY doctor_id) TO 'smartcare_doctors.csv' "
        "WITH (FORMAT csv, HEADER false)";

    if (!db.execute(query))
        return;

    ifstream file("smartcare_doctors.csv");

    if (!file)
        return;

    string line;

    while (getline(file, line))
    {
        if (line.empty())
            continue;

        size_t p1 = line.find(',');
        size_t p2 = line.find(',', p1 + 1);

        if (p1 == string::npos || p2 == string::npos)
            continue;

        string idText = line.substr(0, p1);
        string name = line.substr(p1 + 1, p2 - p1 - 1);
        string specialization = line.substr(p2 + 1);

        if (name.size() >= 2 && name[0] == '"' &&
            name[name.size() - 1] == '"')
        {
            name = name.substr(1, name.size() - 2);
        }

        if (specialization.size() >= 2 &&
            specialization[0] == '"' &&
            specialization[specialization.size() - 1] == '"')
        {
            specialization =
                specialization.substr(1, specialization.size() - 2);
        }

        int id = atoi(idText.c_str());

        if (!doctorExists(doctors, id))
        {
            doctors.push_back(
                Doctor(id, name, specialization)
            );
        }
    }

    file.close();
    remove("smartcare_doctors.csv");
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    Database db;

    if (!db.isConnected())
    {
        cout << "\nPlease check PostgreSQL and password.\n";
        return 0;
    }

    createTables(db);


    // --------------------------------------------------------
    // DATA STRUCTURES
    // --------------------------------------------------------

    list<Patient> patients;

    queue<Appointment> appointments;

    priority_queue<
        Emergency,
        vector<Emergency>,
        CompareEmergency
    > emergencies;

    stack<string> actions;

    unordered_map<int, Patient> patientHash;

    vector<Doctor> doctors;

    // Load persistent data from PostgreSQL into C++ structures.
    loadPatientsFromDatabase(db, patients, patientHash);
    loadDoctorsFromDatabase(db, doctors);

    cout << "\nExisting database data loaded into C++.";
    cout << "\nPatients loaded: " << patients.size();
    cout << "\nDoctors loaded: " << doctors.size() << "\n";


    // --------------------------------------------------------
    // HOSPITAL NETWORK
    // --------------------------------------------------------

    HospitalNetwork hospitalNetwork;

    hospitalNetwork.addConnection(
        "City Hospital", "Apollo Hospital");

    hospitalNetwork.addConnection(
        "Apollo Hospital", "Fortis Hospital");

    hospitalNetwork.addConnection(
        "Fortis Hospital", "Max Hospital");

    hospitalNetwork.addConnection(
        "City Hospital", "AIIMS");


    int choice;


    // ========================================================
    // MAIN MENU
    // ========================================================

    do
    {
        displayHeader();

        cout << "\n1.  Add Patient";
        cout << "\n2.  Display Patients";
        cout << "\n3.  Search Patient";
        cout << "\n4.  Add Doctor";
        cout << "\n5.  Display Doctors";
        cout << "\n6.  Book Appointment";
        cout << "\n7.  Process Appointment";
        cout << "\n8.  Add Emergency Case";
        cout << "\n9.  Process Emergency Case";
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
        cout << "\n0.  Exit";

        cout << "\n\nEnter your choice: ";

        if (!(cin >> choice))
        {
            clearInput();
            cout << "\nInvalid input. Enter a number.\n";
            continue;
        }


        // ====================================================
        // ADD PATIENT
        // ====================================================

        if (choice == 1)
        {
            int id, age;
            string name;
            char gender;

            cout << "\nEnter Patient ID: ";
            cin >> id;

            if (patientExists(patients, id))
            {
                cout << "Patient already exists.\n";
                continue;
            }

            cout << "Enter Patient Name: ";
            cin.ignore();
            getline(cin, name);

            cout << "Enter Age: ";
            cin >> age;

            cout << "Enter Gender (M/F): ";
            cin >> gender;

            Patient p(id, name, age, gender);

            patients.push_back(p);
            patientHash.emplace(id, p);

            string query =
                "INSERT INTO patients "
                "(patient_id, name, age, gender) VALUES (" +
                to_string(id) + ", '" +
                escapeSQL(name) + "', " +
                to_string(age) + ", '" +
                string(1, gender) + "')";

            if (db.execute(query))
            {
                cout << "\nPatient added successfully!";
                cout << "\nPatient stored in PostgreSQL!";
                actions.push("Added Patient: " + name);
            }
        }


        // ====================================================
        // DISPLAY PATIENTS
        // ====================================================

        else if (choice == 2)
        {
            cout << "\n========== PATIENTS ==========\n";

            if (patients.empty())
            {
                cout << "No patients available.\n";
            }
            else
            {
                for (list<Patient>::iterator it = patients.begin();
                     it != patients.end();
                     ++it)
                {
                    it->display();
                }
            }
        }


        // ====================================================
        // SEARCH PATIENT
        // ====================================================

        else if (choice == 3)
        {
            int id;

            cout << "\nEnter Patient ID to search: ";
            cin >> id;

            unordered_map<int, Patient>::iterator it =
                patientHash.find(id);

            if (it != patientHash.end())
            {
                cout << "\nPatient Found:\n";
                it->second.display();
            }
            else
            {
                cout << "\nPatient not found.\n";
            }
        }


        // ====================================================
        // ADD DOCTOR
        // ====================================================

        else if (choice == 4)
        {
            int id;
            string name, specialization;

            cout << "\nEnter Doctor ID: ";
            cin >> id;

            if (doctorExists(doctors, id))
            {
                cout << "Doctor already exists.\n";
                continue;
            }

            cout << "Enter Doctor Name: ";
            cin.ignore();
            getline(cin, name);

            cout << "Enter Specialization: ";
            getline(cin, specialization);

            Doctor d(id, name, specialization);

            doctors.push_back(d);

            string query =
                "INSERT INTO doctors "
                "(doctor_id, name, specialization) VALUES (" +
                to_string(id) + ", '" +
                escapeSQL(name) + "', '" +
                escapeSQL(specialization) + "')";

            if (db.execute(query))
            {
                cout << "\nDoctor added successfully!";
                cout << "\nDoctor stored in PostgreSQL!";

                actions.push("Added Doctor: " + name);
            }
        }


        // ====================================================
        // DISPLAY DOCTORS
        // ====================================================

        else if (choice == 5)
        {
            cout << "\n========== DOCTORS ==========\n";

            if (doctors.empty())
            {
                cout << "No doctors available.\n";
            }
            else
            {
                for (size_t i = 0;
                     i < doctors.size();
                     i++)
                {
                    doctors[i].display();
                }
            }
        }


        // ====================================================
        // BOOK APPOINTMENT
        // ====================================================

        else if (choice == 6)
        {
            int patientID, doctorID;

            cout << "\nEnter Patient ID: ";
            cin >> patientID;

            if (!patientExists(patients, patientID))
            {
                cout << "Patient does not exist.\n";
                continue;
            }

            cout << "Enter Doctor ID: ";
            cin >> doctorID;

            if (!doctorExists(doctors, doctorID))
            {
                cout << "Doctor does not exist.\n";
                continue;
            }

            Appointment a(patientID, doctorID);

            appointments.push(a);

            string query =
                "INSERT INTO appointments "
                "(patient_id, doctor_id) VALUES (" +
                to_string(patientID) + ", " +
                to_string(doctorID) + ")";

            if (db.execute(query))
            {
                cout << "\nAppointment booked successfully!";
                cout << "\nAppointment stored in PostgreSQL!";

                actions.push("Booked Appointment");
            }
        }


        // ====================================================
        // PROCESS APPOINTMENT
        // ====================================================

        else if (choice == 7)
        {
            if (appointments.empty())
            {
                cout << "\nNo appointments waiting.\n";
            }
            else
            {
                Appointment a = appointments.front();
                appointments.pop();

                cout << "\nProcessing Appointment:\n";
                a.display();

                actions.push("Processed Appointment");
            }
        }


        // ====================================================
        // ADD EMERGENCY
        // ====================================================

        else if (choice == 8)
        {
            int patientID;
            int priority;

            cout << "\nEnter Patient ID: ";
            cin >> patientID;

            if (!patientExists(patients, patientID))
            {
                cout << "Patient does not exist.\n";
                continue;
            }

            cout << "\nPriority Levels:\n";
            cout << "1. Critical\n";
            cout << "2. Very Serious\n";
            cout << "3. Serious\n";
            cout << "4. Moderate\n";
            cout << "5. Low\n";

            cout << "\nEnter Priority: ";
            cin >> priority;

            if (priority < 1 || priority > 5)
            {
                cout << "Invalid priority.\n";
                continue;
            }

            Emergency e(patientID, priority);

            emergencies.push(e);

            string query =
                "INSERT INTO emergency "
                "(patient_id, priority) VALUES (" +
                to_string(patientID) + ", " +
                to_string(priority) + ")";

            if (db.execute(query))
            {
                cout << "\nEmergency case added!";
                cout << "\nEmergency data stored in PostgreSQL!";

                actions.push("Added Emergency Case");
            }
        }


        // ====================================================
        // PROCESS EMERGENCY
        // ====================================================

        else if (choice == 9)
        {
            if (emergencies.empty())
            {
                cout << "\nNo emergency cases.\n";
            }
            else
            {
                Emergency e = emergencies.top();
                emergencies.pop();

                cout << "\nProcessing Emergency Case:\n";
                e.display();

                actions.push("Processed Emergency Case");
            }
        }


        // ====================================================
        // ADD MEDICAL RECORD
        // ====================================================

        else if (choice == 10)
        {
            int patientID;
            string diagnosis;
            string prescription;

            cout << "\nEnter Patient ID: ";
            cin >> patientID;

            if (!patientExists(patients, patientID))
            {
                cout << "Patient does not exist.\n";
                continue;
            }

            cout << "Enter Disease: ";
            cin.ignore();
            getline(cin, diagnosis);

            cout << "Enter Treatment: ";
            getline(cin, prescription);

            string query =
                "INSERT INTO medical_records "
                "(patient_id, disease, treatment, medicine) VALUES (" +
                to_string(patientID) + ", '" +
                escapeSQL(diagnosis) + "', '" +
                escapeSQL(prescription) + "', '')";

            if (db.execute(query))
            {
                cout << "\nMedical record added!";
                cout << "\nMedical record stored in PostgreSQL!";

                actions.push("Added Medical Record");
            }
        }


        // ====================================================
        // VIEW MEDICAL RECORDS
        // ====================================================

        else if (choice == 11)
        {
            cout << "\n========== MEDICAL RECORDS ==========\n";

            db.showQuery(
                "SELECT * FROM medical_records"
            );
        }


        // ====================================================
        // DATABASE PATIENTS
        // ====================================================

        else if (choice == 12)
        {
            cout << "\n========== DATABASE PATIENTS ==========\n";

            db.showQuery(
                "SELECT * FROM patients ORDER BY patient_id"
            );
        }


        // ====================================================
        // DATABASE DOCTORS
        // ====================================================

        else if (choice == 13)
        {
            cout << "\n========== DATABASE DOCTORS ==========\n";

            db.showQuery(
                "SELECT * FROM doctors ORDER BY doctor_id"
            );
        }


        // ====================================================
        // DATABASE APPOINTMENTS
        // ====================================================

        else if (choice == 14)
        {
            cout << "\n========== DATABASE APPOINTMENTS ==========\n";

            db.showQuery(
                "SELECT * FROM appointments "
                "ORDER BY appointment_id"
            );
        }


        // ====================================================
        // DATABASE EMERGENCY
        // ====================================================

        else if (choice == 15)
        {
            cout << "\n========== DATABASE EMERGENCY ==========\n";

            db.showQuery(
                "SELECT * FROM emergency "
                "ORDER BY priority"
            );
        }


        // ====================================================
        // HOSPITAL NETWORK
        // ====================================================

        else if (choice == 16)
        {
            hospitalNetwork.displayNetwork();
        }


        // ====================================================
        // BFS
        // ====================================================

        else if (choice == 17)
        {
            string start;

            cout << "\nEnter Starting Hospital: ";

            cin.ignore();
            getline(cin, start);

            hospitalNetwork.BFS(start);
        }


        // ====================================================
        // DFS
        // ====================================================

        else if (choice == 18)
        {
            string start;

            cout << "\nEnter Starting Hospital: ";

            cin.ignore();
            getline(cin, start);

            hospitalNetwork.DFS(start);
        }


        // ====================================================
        // RECENT ACTIONS
        // ====================================================

        else if (choice == 19)
        {
            if (actions.empty())
            {
                cout << "\nNo recent actions.\n";
            }
            else
            {
                stack<string> temp = actions;

                cout << "\n========== RECENT ACTIONS ==========\n";

                while (!temp.empty())
                {
                    cout << temp.top() << endl;
                    temp.pop();
                }
            }
        }


        // ====================================================
        // EXIT
        // ====================================================

        else if (choice == 0)
        {
            cout << "\nThank you for using SmartCare!\n";
        }

        else
        {
            cout << "\nInvalid choice.\n";
        }

    } while (choice != 0);
    return 0;
}
