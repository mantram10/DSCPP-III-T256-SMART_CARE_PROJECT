#include <iostream>
#include <string>
#include <list>
#include <queue>
#include <stack>
#include <unordered_map>
#include <vector>
#include <limits>
#include <cctype>

using namespace std;

// =====================================================
// PATIENT CLASS
// =====================================================

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
        cout << "Patient ID       : " << id << endl;
        cout << "Patient Name     : " << name << endl;
        cout << "Age              : " << age << endl;
        cout << "Gender           : " << gender << endl;
    }
};

// =====================================================
// DOCTOR CLASS
// =====================================================

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
        cout << "Doctor ID        : " << id << endl;
        cout << "Doctor Name      : " << name << endl;
        cout << "Specialization   : " << specialization << endl;
    }
};

// =====================================================
// APPOINTMENT CLASS
// =====================================================

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
        cout << "Patient ID       : " << patientID << endl;
        cout << "Doctor ID        : " << doctorID << endl;
    }
};

// =====================================================
// EMERGENCY CLASS
// =====================================================

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
};

// =====================================================
// PRIORITY COMPARATOR
// Priority 1 = Most Serious
// Priority 5 = Least Serious
// =====================================================

struct CompareEmergency
{
    bool operator()(const Emergency& a,
                    const Emergency& b) const
    {
        return a.priority > b.priority;
    }
};

// =====================================================
// CLEAR INVALID INPUT
// =====================================================

void clearInput()
{
    cin.clear();

    cin.ignore(
        numeric_limits<streamsize>::max(),
        '\n'
    );
}

// =====================================================
// PAUSE SCREEN
// =====================================================

void pauseScreen()
{
    cout << "\nPress ENTER to continue...";
    cin.ignore(
        numeric_limits<streamsize>::max(),
        '\n'
    );
}

// =====================================================
// DISPLAY HEADER
// =====================================================

void displayHeader()
{
    cout << "\n";
    cout << "==============================================\n";
    cout << "          SMARTCARE HEALTHCARE SYSTEM\n";
    cout << "==============================================\n";
}

// =====================================================
// DISPLAY EMERGENCY PRIORITIES
// =====================================================

void displayPriorityLevels()
{
    cout << "\n";
    cout << "------------- EMERGENCY PRIORITY -------------\n";
    cout << "1. CRITICAL\n";
    cout << "2. VERY SERIOUS \n";
    cout << "3. SERIOUS\n";
    cout << "4. MODERATE\n";
    cout << "5. LOW\n";
    cout << "----------------------------------------------\n";
}

// =====================================================
// GET PRIORITY NAME
// =====================================================

string getPriorityName(int priority)
{
    if (priority == 1)
        return "CRITICAL";

    if (priority == 2)
        return "VERY SERIOUS";

    if (priority == 3)
        return "SERIOUS";

    if (priority == 4)
        return "MODERATE";

    return "LOW";
}

// =====================================================
// FIND DOCTOR
// =====================================================

bool doctorExists(
    const vector<Doctor>& doctors,
    int doctorID)
{
    for (vector<Doctor>::const_iterator it =
         doctors.begin();
         it != doctors.end();
         ++it)
    {
        if (it->id == doctorID)
        {
            return true;
        }
    }

    return false;
}

// =====================================================
// MAIN FUNCTION
// =====================================================

int main()
{
    // =================================================
    // DATA STRUCTURES
    // =================================================

    // Linked List for patients
    list<Patient> patients;

    // Queue for appointments
    queue<Appointment> appointments;

    // Priority Queue for emergency cases
    priority_queue<
        Emergency,
        vector<Emergency>,
        CompareEmergency
    > emergencyQueue;

    // Stack for recent actions
    stack<string> actions;

    // Hash Table for fast patient search
    unordered_map<int, Patient> patientSearch;

    // Vector for doctors
    vector<Doctor> doctors;

    int choice = -1;

    // =================================================
    // MAIN PROGRAM LOOP
    // =================================================

    do
    {
        displayHeader();

        cout << "1.  Patient Management\n";
        cout << "2.  Display Patients\n";
        cout << "3.  Search Patient\n";
        cout << "4.  Doctor Management\n";
        cout << "5.  Display Doctors\n";
        cout << "6.  Book Appointment\n";
        cout << "7.  Process Appointment\n";
        cout << "8.  Add Emergency Case\n";
        cout << "9.  Process Emergency Case\n";
        cout << "10. Recent Actions\n";
        cout << "0.  Exit\n";

        cout << "----------------------------------------------\n";
        cout << "Enter your choice: ";

        // =================================================
        // MENU INPUT VALIDATION
        // =================================================

        if (!(cin >> choice))
        {
            cout << "\nInvalid input!";
            cout << " Please enter a number from 0 to 10.\n";

            clearInput();
            choice = -1;

            continue;
        }

        // =================================================
        // 1. ADD PATIENT
        // =================================================

        if (choice == 1)
        {
            int id;
            int age;
            char gender;
            string name;

            cout << "\n------------- ADD PATIENT -------------\n";

            cout << "Enter Patient ID: ";

            if (!(cin >> id))
            {
                cout << "Invalid Patient ID.\n";
                clearInput();
                continue;
            }

            // Duplicate ID check
            if (patientSearch.find(id)
                != patientSearch.end())
            {
                cout << "\nPatient ID "
                     << id
                     << " already exists.\n";

                continue;
            }

            cin.ignore(
                numeric_limits<streamsize>::max(),
                '\n'
            );

            cout << "Enter Patient Name: ";
            getline(cin, name);

            if (name.empty())
            {
                cout << "Patient name cannot be empty.\n";
                continue;
            }

            cout << "Enter Age: ";

            if (!(cin >> age))
            {
                cout << "Invalid age.\n";
                clearInput();
                continue;
            }

            if (age <= 0 || age > 120)
            {
                cout << "Please enter a valid age.\n";
                continue;
            }

            cout << "Enter Gender (M/F): ";
            cin >> gender;

            gender = toupper(gender);

            if (gender != 'M' && gender != 'F')
            {
                cout << "Please enter M or F only.\n";
                continue;
            }

            Patient p(
                id,
                name,
                age,
                gender
            );

            // Add to Linked List
            patients.push_back(p);

            // Add to Hash Table
            patientSearch.emplace(id, p);

            // Add to Stack
            actions.push(
                "Patient added - ID: "
                + to_string(id)
            );

            cout << "\nPatient added successfully!\n";
        }

        // =================================================
        // 2. DISPLAY PATIENTS
        // =================================================

        else if (choice == 2)
        {
            cout << "\n------------- PATIENT RECORDS -------------\n";

            if (patients.empty())
            {
                cout << "No patient records available.\n";
            }
            else
            {
                for (list<Patient>::const_iterator it =
                     patients.begin();
                     it != patients.end();
                     ++it)
                {
                    cout << "\n--------------------------------------------\n";
                    it->display();
                }

                cout << "\n--------------------------------------------\n";
                cout << "Total Patients: "
                     << patients.size()
                     << endl;
            }
        }

        // =================================================
        // 3. SEARCH PATIENT
        // =================================================

        else if (choice == 3)
        {
            int id;

            cout << "\n------------- SEARCH PATIENT -------------\n";

            cout << "Enter Patient ID: ";

            if (!(cin >> id))
            {
                cout << "Invalid Patient ID.\n";
                clearInput();
                continue;
            }

            unordered_map<int, Patient>::iterator found =
                patientSearch.find(id);

            if (found != patientSearch.end())
            {
                cout << "\nPatient Found!\n";
                cout << "--------------------------------------------\n";

                found->second.display();

                cout << "--------------------------------------------\n";
            }
            else
            {
                cout << "\nPatient with ID "
                     << id
                     << " does not exist.\n";
            }
        }

        // =================================================
        // 4. ADD DOCTOR
        // =================================================

        else if (choice == 4)
        {
            int id;
            string name;
            string specialization;

            cout << "\n------------- ADD DOCTOR -------------\n";

            cout << "Enter Doctor ID: ";

            if (!(cin >> id))
            {
                cout << "Invalid Doctor ID.\n";
                clearInput();
                continue;
            }

            // Check duplicate doctor ID
            if (doctorExists(doctors, id))
            {
                cout << "\nDoctor ID "
                     << id
                     << " already exists.\n";

                continue;
            }

            cin.ignore(
                numeric_limits<streamsize>::max(),
                '\n'
            );

            cout << "Enter Doctor Name: ";
            getline(cin, name);

            if (name.empty())
            {
                cout << "Doctor name cannot be empty.\n";
                continue;
            }

            cout << "Enter Specialization: ";
            getline(cin, specialization);

            if (specialization.empty())
            {
                cout << "Specialization cannot be empty.\n";
                continue;
            }

            Doctor d(
                id,
                name,
                specialization
            );

            doctors.push_back(d);

            actions.push(
                "Doctor added - ID: "
                + to_string(id)
            );

            cout << "\nDoctor added successfully!\n";
        }

        // =================================================
        // 5. DISPLAY DOCTORS
        // =================================================

        else if (choice == 5)
        {
            cout << "\n------------- DOCTORS -------------\n";

            if (doctors.empty())
            {
                cout << "No doctors available.\n";
            }
            else
            {
                for (vector<Doctor>::const_iterator it =
                     doctors.begin();
                     it != doctors.end();
                     ++it)
                {
                    cout << "\n-----------------------------------\n";
                    it->display();
                }

                cout << "\n-----------------------------------\n";
                cout << "Total Doctors: "
                     << doctors.size()
                     << endl;
            }
        }

        // =================================================
        // 6. BOOK APPOINTMENT
        // =================================================

        else if (choice == 6)
        {
            int patientID;
            int doctorID;

            cout << "\n----------- BOOK APPOINTMENT -----------\n";

            cout << "Enter Patient ID: ";

            if (!(cin >> patientID))
            {
                cout << "Invalid Patient ID.\n";
                clearInput();
                continue;
            }

            // Check patient
            if (patientSearch.find(patientID)
                == patientSearch.end())
            {
                cout << "\nPatient with ID "
                     << patientID
                     << " does not exist.\n";

                cout << "Please add the patient first.\n";

                continue;
            }

            cout << "Enter Doctor ID: ";

            if (!(cin >> doctorID))
            {
                cout << "Invalid Doctor ID.\n";
                clearInput();
                continue;
            }

            // Check doctor
            if (!doctorExists(doctors, doctorID))
            {
                cout << "\nDoctor with ID "
                     << doctorID
                     << " does not exist.\n";

                cout << "Please select an available doctor.\n";

                continue;
            }

            Appointment a(
                patientID,
                doctorID
            );

            // Add appointment to Queue
            appointments.push(a);

            // Add action to Stack
            actions.push(
                "Appointment booked - Patient ID: "
                + to_string(patientID)
            );

            cout << "\nAppointment booked successfully!\n";
            cout << "Appointment has been added to the queue.\n";
        }

        // =================================================
        // 7. PROCESS APPOINTMENT
        // =================================================

        else if (choice == 7)
        {
            cout << "\n----------- PROCESS APPOINTMENT -----------\n";

            if (appointments.empty())
            {
                cout << "No appointments are currently waiting.\n";
            }
            else
            {
                Appointment a =
                    appointments.front();

                cout << "\nNext Appointment:\n";
                cout << "--------------------------------------------\n";

                a.display();

                cout << "--------------------------------------------\n";

                appointments.pop();

                actions.push(
                    "Appointment processed - Patient ID: "
                    + to_string(a.patientID)
                );

                cout << "Appointment processed successfully!\n";
            }
        }

        // =================================================
        // 8. ADD EMERGENCY CASE
        // =================================================

        else if (choice == 8)
        {
            int patientID;
            int priority;

            cout << "\n----------- EMERGENCY CASE -----------\n";

            displayPriorityLevels();

            cout << "\nEnter Patient ID: ";

            if (!(cin >> patientID))
            {
                cout << "Invalid Patient ID.\n";
                clearInput();
                continue;
            }

            // Check patient
            if (patientSearch.find(patientID)
                == patientSearch.end())
            {
                cout << "\nPatient with ID "
                     << patientID
                     << " does not exist.\n";

                continue;
            }

            cout << "Enter Priority (1-5): ";

            if (!(cin >> priority))
            {
                cout << "Invalid priority.\n";
                clearInput();
                continue;
            }

            if (priority < 1 || priority > 5)
            {
                cout << "\nInvalid priority!\n";
                cout << "Please select a value from 1 to 5.\n";

                continue;
            }

            Emergency e(
                patientID,
                priority
            );

            // Add to Priority Queue
            emergencyQueue.push(e);

            actions.push(
                "Emergency case added - Patient ID: "
                + to_string(patientID)
            );

            cout << "\nEmergency case added successfully!\n";

            cout << "Priority Level : "
                 << priority
                 << endl;

            cout << "Seriousness    : "
                 << getPriorityName(priority)
                 << endl;
        }

        // =================================================
        // 9. PROCESS EMERGENCY CASE
        // =================================================

        else if (choice == 9)
        {
            cout << "\n--------- PROCESS EMERGENCY ---------\n";

            if (emergencyQueue.empty())
            {
                cout << "No emergency cases waiting.\n";
            }
            else
            {
                Emergency e =
                    emergencyQueue.top();

                cout << "\nNext Emergency Case:\n";
                cout << "--------------------------------------------\n";

                cout << "Patient ID   : "
                     << e.patientID
                     << endl;

                cout << "Priority     : "
                     << e.priority
                     << endl;

                cout << "Seriousness  : "
                     << getPriorityName(e.priority)
                     << endl;

                cout << "--------------------------------------------\n";

                emergencyQueue.pop();

                actions.push(
                    "Emergency processed - Patient ID: "
                    + to_string(e.patientID)
                );

                cout << "Emergency case processed successfully!\n";
            }
        }

        // =================================================
        // 10. RECENT ACTIONS
        // =================================================

        else if (choice == 10)
        {
            cout << "\n----------- RECENT ACTIONS -----------\n";

            if (actions.empty())
            {
                cout << "No actions have been recorded yet.\n";
            }
            else
            {
                stack<string> temp = actions;

                int count = 1;

                while (!temp.empty())
                {
                    cout << count
                         << ". "
                         << temp.top()
                         << endl;

                    temp.pop();

                    count++;
                }
            }
        }

        // =================================================
        // 0. EXIT
        // =================================================

        else if (choice == 0)
        {
            cout << "\n==============================================\n";
            cout << "        Thank you for using SmartCare!\n";
            cout << "==============================================\n";
        }

        // =================================================
        // INVALID OPTION
        // =================================================

        else
        {
            cout << "\nInvalid choice!\n";
            cout << "Please select a number from 0 to 10.\n";
        }

    }
    while (choice != 0);

    return 0;
}
