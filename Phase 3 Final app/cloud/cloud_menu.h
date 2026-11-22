// ============================================================
// cloud_menu.h  -  SmartCare Phase 3
// Menu actions that connect Firebase -> validation -> PostgreSQL ->
// in-memory C++ structures.
//
// Templated on the app's own Database and Patient classes, so it
// works with phase2.cpp's existing code without modifying them:
//   DB must provide:       bool execute(std::string), void showQuery(std::string)
//   PatientT must provide: PatientT(int id, std::string name, int age, char gender)
// ============================================================
#ifndef SMARTCARE_CLOUD_MENU_H
#define SMARTCARE_CLOUD_MENU_H

#include "cloud_import.h"
#include "firebase_client.h"

#include <iomanip>
#include <iostream>
#include <limits>
#include <ctime>
#include <list>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#endif

namespace smartcare {

// Reads a password without echoing it on Windows consoles.
inline std::string readPasswordHidden()
{
#ifdef _WIN32
    std::string pw;
    int ch;
    while ((ch = _getch()) != '\r' && ch != '\n')
    {
        if (ch == 8) // backspace
        {
            if (!pw.empty())
            {
                pw.pop_back();
                std::cout << "\b \b";
            }
        }
        else if (ch >= 32 && ch < 127)
        {
            pw += static_cast<char>(ch);
            std::cout << '*';
        }
    }
    std::cout << '\n';
    return pw;
#else
    std::string pw;
    std::cin >> pw;
    return pw;
#endif
}

inline void cloudSignIn(FirebaseClient &fb, std::stack<std::string> &actions)
{
    std::string problem = fb.configProblem();
    if (!problem.empty())
    {
        std::cout << "\nCloud is not configured: " << problem
                  << "\nSee docs/PHASE3_IMPLEMENTATION_GUIDE.md (environment setup).\n";
        return;
    }
    // The environment value is only a default, so the demo can switch
    // between hospital-a and hospital-b without restarting.
    std::string defaultEmail = readEnv("SMARTCARE_HOSPITAL_EMAIL");
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // rest of menu line
    std::cout << "\nHospital account email";
    if (!defaultEmail.empty())
        std::cout << " [Enter = " << defaultEmail << "]";
    std::cout << ": ";
    std::string email;
    std::getline(std::cin, email);
    email = detail::trim(email); // ignore spaces typed before/after the email
    if (email.empty())
        email = defaultEmail;
    if (email.empty())
    {
        std::cout << "No email entered.\n";
        return;
    }
    std::cout << "Password: ";
    std::string password = readPasswordHidden();

    std::string message;
    bool ok = fb.signIn(email, password, message);
    password.assign(password.size(), '\0'); // best-effort wipe
    std::cout << '\n' << message << '\n';
    if (ok)
        actions.push("Cloud sign-in: " + fb.hospital());
}

template <class DB, class PatientT>
void cloudImportPatients(FirebaseClient &fb, DB &db, const std::string &cloudPath, ImportMode mode,
                         const std::string &expectedHospital, std::list<PatientT> &patients,
                         std::unordered_map<int, PatientT> &patientHash,
                         std::stack<std::string> &actions)
{
    if (!fb.isSignedIn())
    {
        std::cout << "\nPlease sign in to the cloud first.\n";
        return;
    }

    std::cout << "\nRetrieving " << cloudPath << " from Firebase...\n";
    HttpResult r = fb.get(cloudPath);

    if (r.status == 0)
    {
        std::cout << "Cloud request failed: " << r.error
                  << "\nPostgreSQL data is unaffected. Check the internet connection and retry.\n";
        return;
    }
    if (r.status == 401 || r.status == 403)
    {
        std::cout << "ACCESS DENIED (HTTP " << r.status << ") for " << cloudPath
                  << "\nThe Firebase security rules do not allow " << fb.hospital()
                  << " to read this path.\n";
        actions.push("Cloud access denied: " + cloudPath);
        return;
    }
    if (!r.ok())
    {
        std::cout << "Cloud request failed with HTTP " << r.status << ": " << r.body << '\n';
        return;
    }

    JsonValue root;
    try
    {
        root = JsonParser::parse(r.body);
    }
    catch (const std::exception &e)
    {
        std::cout << "Invalid data from cloud: " << e.what() << '\n';
        return;
    }

    ImportReport rep = validatePatients(root, mode, expectedHospital, [&](int id) {
        return patientHash.count(id) > 0; // O(1) average lookup
    });

    if (!rep.fatalError.empty())
    {
        std::cout << "Cannot import: " << rep.fatalError << '\n';
        return;
    }

    std::cout << "\n========== CLOUD VALIDATION REPORT ==========\n";
    std::cout << "Records received : " << rep.total << '\n';
    std::cout << "Approved         : " << rep.accepted.size() << '\n';
    std::cout << "Rejected/Skipped : " << rep.issues.size() << "\n\n";

    for (const CloudPatient &p : rep.accepted)
        std::cout << "  OK   " << std::left << std::setw(9) << p.id << std::setw(26) << p.name
                  << " age " << std::setw(4) << p.age << p.gender << "  from " << p.sourceHospital
                  << '\n';
    for (const ImportIssue &is : rep.issues)
        std::cout << "  " << (is.status == "SKIPPED" ? "SKIP " : "FAIL ") << std::left
                  << std::setw(9) << is.recordKey << " " << is.reason << '\n';

    if (rep.accepted.empty() && rep.issues.empty())
    {
        std::cout << "Nothing to import (path is empty).\n";
        return;
    }

    char confirm = 'n';
    if (!rep.accepted.empty())
    {
        std::cout << "\nImport " << rep.accepted.size() << " approved record(s) into PostgreSQL? (y/n): ";
        std::cin >> confirm;
    }
    if (confirm != 'y' && confirm != 'Y')
    {
        // Still record rejected/skipped items so the validation is auditable.
        ImportReport logOnly;
        logOnly.issues = rep.issues;
        if (!logOnly.issues.empty())
            db.execute(buildImportSql(logOnly, cloudPath));
        std::cout << "No records imported. Validation results written to cloud_import_log.\n";
        return;
    }

    // Write to the database FIRST; update memory only if the transaction succeeded,
    // so C++ structures never show rows that PostgreSQL does not have.
    if (!db.execute(buildImportSql(rep, cloudPath)))
    {
        std::cout << "Database import failed; transaction rolled back. Nothing was imported.\n"
                  << "Did you run database/phase3_migration.sql?\n";
        return;
    }

    for (const CloudPatient &p : rep.accepted)
    {
        PatientT obj(p.id, p.name, p.age, p.gender);
        patients.push_back(obj);
        patientHash.emplace(p.id, obj);
    }
    std::cout << rep.accepted.size() << " record(s) imported into PostgreSQL and loaded into SmartCare.\n";
    actions.push("Imported " + std::to_string(rep.accepted.size()) + " patient(s) from " + cloudPath);
}

// Reads an int with cin >> (same style as the cloud menu). Returns false on bad input.
inline bool cloudReadInt(const std::string &prompt, int &out)
{
    std::cout << prompt;
    if (std::cin >> out)
        return true;
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return false;
}

// Lists every hospital except 'exclude' and asks for one. Returns "" if cancelled.
inline std::string cloudChooseHospital(const std::string &exclude, const std::string &title)
{
    std::vector<std::string> others;
    for (size_t i = 0; i < hospitalRanges().size(); i++)
        if (exclude != hospitalRanges()[i].hospital)
            others.push_back(hospitalRanges()[i].hospital);
    std::cout << "\n" << title << ":\n";
    for (size_t i = 0; i < others.size(); i++)
        std::cout << "  " << (i + 1) << ". " << others[i] << '\n';
    int pick = 0;
    if (!cloudReadInt("Choose: ", pick) || pick < 1 || pick > (int)others.size())
    {
        std::cout << "Cancelled.\n";
        return "";
    }
    return others[pick - 1];
}

// Asks which OTHER hospital to share with / revoke from. Returns "" if cancelled.
inline std::string cloudChooseOtherHospital(const std::string &mine)
{
    return cloudChooseHospital(mine, "Receiving hospital");
}

inline std::string utcTimestamp()
{
    std::time_t t = std::time(nullptr);
    char buf[40];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+00:00", std::gmtime(&t));
    return buf;
}

// Share one patient from MY hospital's cloud dataset with another hospital.
// The app checks consent and data; the Firebase rules check it again on the server.
inline void cloudSharePatient(FirebaseClient &fb, std::stack<std::string> &actions)
{
    if (!fb.isSignedIn() || !rangeFor(fb.hospital()))
    {
        std::cout << "\nSign in with a hospital account first.\n";
        return;
    }
    const std::string mine = fb.hospital();
    const HospitalIdRange *range = rangeFor(mine);

    std::string target = cloudChooseOtherHospital(mine);
    if (target.empty())
        return;

    int id = 0;
    if (!cloudReadInt("Patient ID to share (" + std::to_string(range->minId) + "-" +
                          std::to_string(range->maxId) + "): ", id) ||
        id < range->minId || id > range->maxId)
    {
        std::cout << "You can only share patients of " << mine << ".\n";
        return;
    }
    const std::string key = "P" + std::to_string(id);

    // 1. Read the patient from my own cloud dataset
    HttpResult r = fb.get("SmartCare/" + mine + "/Patients/" + key);
    if (r.status == 0)
    {
        std::cout << "Cloud request failed: " << r.error << '\n';
        return;
    }
    if (!r.ok())
    {
        std::cout << "Could not read patient (HTTP " << r.status << ").\n";
        return;
    }

    JsonValue rec;
    try
    {
        rec = JsonParser::parse(r.body);
    }
    catch (const std::exception &e)
    {
        std::cout << "Invalid data from cloud: " << e.what() << '\n';
        return;
    }
    if (rec.isNull())
    {
        std::cout << key << " is not in " << mine << "'s cloud dataset.\n";
        return;
    }

    // 2. Validate with the same rules used for imports
    JsonValue wrapper;
    wrapper.type = JsonValue::Object;
    wrapper.keys.push_back(key);
    wrapper.items.push_back(rec);
    ImportReport check = validatePatients(wrapper, ImportMode::HospitalDataset, mine, nullptr);
    if (check.accepted.size() != 1)
    {
        std::cout << "Record failed validation: "
                  << (check.issues.empty() ? check.fatalError : check.issues[0].reason) << '\n';
        return;
    }
    const CloudPatient &p = check.accepted[0];

    std::cout << "\nPatient to share:\n  " << p.id << "  " << p.name << ", age " << p.age
              << ", " << p.gender << "\n  From " << mine << "  ->  To " << target << '\n';

    // 3. Consent
    char consent = 'n';
    std::cout << "\nHas the patient given consent to share this record with " << target
              << "? (y/n): ";
    std::cin >> consent;
    if (consent != 'y' && consent != 'Y')
    {
        std::cout << "NOT shared: patient consent is required.\n";
        actions.push("Share refused (no consent): " + key);
        return;
    }

    // 4. Write to Shared_Records/<target>/<key>
    std::string json = "{\"patient_id\":" + std::to_string(p.id) + ",\"name\":\"" + jsonEscape(p.name) +
                       "\",\"age\":" + std::to_string(p.age) + ",\"gender\":\"" + std::string(1, p.gender) +
                       "\",\"source_hospital\":\"" + mine + "\",\"shared_with\":\"" + target +
                       "\",\"consent\":true,\"shared_at\":\"" + utcTimestamp() + "\"}";

    HttpResult w = fb.put("SmartCare/Shared_Records/" + target + "/" + key, json);
    if (w.status == 0)
        std::cout << "Cloud request failed: " << w.error << '\n';
    else if (w.status == 401 || w.status == 403)
        std::cout << "DENIED by Firebase rules (HTTP " << w.status << "). Were the new rules published?\n";
    else if (!w.ok())
        std::cout << "Share failed with HTTP " << w.status << ": " << w.body << '\n';
    else
    {
        std::cout << "SHARED " << key << " from " << mine << " to " << target << ".\n"
                  << target << " can now import it (Cloud menu option 4).\n";
        actions.push("Shared " + key + " with " + target);
    }
}

// Withdraw a record this hospital shared earlier.
inline void cloudRevokeShare(FirebaseClient &fb, std::stack<std::string> &actions)
{
    if (!fb.isSignedIn() || !rangeFor(fb.hospital()))
    {
        std::cout << "\nSign in with a hospital account first.\n";
        return;
    }
    std::string target = cloudChooseOtherHospital(fb.hospital());
    if (target.empty())
        return;
    int id = 0;
    if (!cloudReadInt("Patient ID to revoke: ", id) || id <= 0)
    {
        std::cout << "Invalid ID.\n";
        return;
    }
    const std::string key = "P" + std::to_string(id);

    HttpResult r = fb.remove("SmartCare/Shared_Records/" + target + "/" + key);
    if (r.status == 0)
        std::cout << "Cloud request failed: " << r.error << '\n';
    else if (r.status == 401 || r.status == 403)
        std::cout << "DENIED (HTTP " << r.status << "): " << key << " does not exist in " << target
                  << "'s shared records, or it was not shared by " << fb.hospital() << ".\n";
    else if (!r.ok())
        std::cout << "Revoke failed with HTTP " << r.status << '\n';
    else
    {
        std::cout << "REVOKED " << key << ": " << target << " can no longer import it.\n"
                  << "(Copies already imported into " << target << "'s database are not deleted.)\n";
        actions.push("Revoked " + key + " from " + target);
    }
}

template <class DB>
void cloudShowImportLog(DB &db)
{
    db.showQuery("SELECT log_id, to_char(imported_at, 'YYYY-MM-DD HH24:MI') AS at, cloud_path, "
                 "record_key, status, reason FROM cloud_import_log ORDER BY log_id DESC LIMIT 25;");
}

// Complete cloud sub-menu. Call it from main(), e.g. menu option 20.
template <class DB, class PatientT>
void cloudMenu(FirebaseClient &fb, DB &db, std::list<PatientT> &patients,
               std::unordered_map<int, PatientT> &patientHash, std::stack<std::string> &actions)
{
    int choice = -1;
    do
    {
        std::cout << "\n========== CLOUD (FIREBASE) ==========";
        std::cout << "\nStatus: "
                  << (fb.isSignedIn() ? "signed in as " + fb.email() + " [" + fb.hospital() + "]"
                                      : std::string("not signed in"));
        std::cout << "\n1. Sign in as hospital";
        std::cout << "\n2. Retrieve & import a hospital's patients (choose hospital)";
        std::cout << "\n3. Retrieve & import MY hospital's patients";
        std::cout << "\n4. Retrieve & import records shared with my hospital";
        std::cout << "\n5. View cloud import log";
        std::cout << "\n6. Share one of my patients with another hospital";
        std::cout << "\n7. Revoke a patient I shared";
        std::cout << "\n0. Back";
        std::cout << "\n\nEnter your choice: ";

        if (!(std::cin >> choice))
        {
            std::cin.clear();
            std::cin.ignore(10000, '\n');
            std::cout << "\nInvalid input. Enter a number.\n";
            choice = -1;
            continue;
        }

        if (choice == 1)
            cloudSignIn(fb, actions);
        else if (choice == 2 || choice == 3)
        {
            std::string h;
            if (choice == 3)
            {
                h = fb.hospital();
                if (!fb.isSignedIn() || !rangeFor(h))
                    std::cout << "\nSign in with a hospital account first.\n";
            }
            else
                h = cloudChooseHospital("", "Which hospital's dataset");
            if (rangeFor(h)) // reading another hospital's data is expected to be DENIED by the rules
                cloudImportPatients(fb, db, "SmartCare/" + h + "/Patients", ImportMode::HospitalDataset, h,
                                    patients, patientHash, actions);
        }
        else if (choice == 4)
        {
            if (!fb.isSignedIn() || fb.hospital().empty())
                std::cout << "\nSign in with a hospital account first.\n";
            else
                cloudImportPatients(fb, db, "SmartCare/Shared_Records/" + fb.hospital(),
                                    ImportMode::SharedRecords, fb.hospital(), patients, patientHash,
                                    actions);
        }
        else if (choice == 5)
            cloudShowImportLog(db);
        else if (choice == 6)
            cloudSharePatient(fb, actions);
        else if (choice == 7)
            cloudRevokeShare(fb, actions);
        else if (choice != 0)
            std::cout << "\nInvalid choice.\n";
    } while (choice != 0);
}

} // namespace smartcare

#endif
