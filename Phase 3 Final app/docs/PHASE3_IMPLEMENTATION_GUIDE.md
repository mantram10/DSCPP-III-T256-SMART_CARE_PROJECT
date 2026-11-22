# SmartCare Phase 3 — Implementation Guide

This package adds the Firebase cloud-exchange layer to SmartCare and lists the fixes the existing `phase2.cpp` needs to pass the Phase 3 tests. PostgreSQL stays the primary database; Firebase is only a controlled exchange layer for synthetic hospital datasets.

## 1. What is in the package

| Path | Purpose |
|---|---|
| `cloud/json_mini.h` | Small JSON parser (recursive descent) for Firebase responses |
| `cloud/firebase_client.h` | Firebase sign-in + authenticated REST reads via `curl.exe` |
| `cloud/cloud_import.h` | Validation rules + transactional SQL builder |
| `cloud/cloud_menu.h` | Cloud sub-menu; works with your existing `Database` and `Patient` classes |
| `python/validation.py` | Same validation rules on the Python side |
| `python/generate_hospital_datasets.py` | Synthetic Hospital A/B datasets (+ optional faulty test file) |
| `python/firebase_admin_tool.py` | Create hospital accounts, upload, share, revoke |
| `firebase/database.rules.json` | Security rules: each hospital reads only its own data |
| `database/phase3_migration.sql` | Constraints, queue status columns, provenance, import log |
| `tests/test_cloud_import.cpp` | 53 automated checks (parser, validator, SQL, client) |
| `tests/mock_firebase_server.py` | **Local test only** — imitates Firebase REST for offline testing |
| `config/set_env.example.ps1` | Environment variable template (copy, fill, never commit) |
| `dataset/*.csv` | Generated synthetic datasets (seed 42, 50 patients per hospital) |

## 2. Architecture and design decisions

```
Python generator ──validate──> CSV ──firebase_admin_tool upload──> Firebase RTDB
                                                                     │  (rules: hospital claim)
C++ SmartCare ──sign in (Auth REST)──> ID token ──GET path.json?auth=token┘
      │ re-validate every record (never trust the cloud)
      ├─ preview + user approval
      ├─ one SQL transaction ──> PostgreSQL (patients + cloud_import_log)
      └─ only after success ──> list<Patient> + unordered_map<int,Patient>
```

Decisions worth being able to defend in the viva:

1. **curl via `system()`, not a C++ HTTP library.** Your app already runs `psql.exe` the same way, and `curl.exe` ships with Windows 10 (1803+) and 11. Cost: one process per request and the token appears briefly on the process command line. Alternative: libcurl/cpr (better for production, harder to build on Windows).
2. **Every value placed on the shell command line is whitelisted** (URL, API key, token, path). This prevents shell injection through configuration or data.
3. **Access control lives in Firebase rules, not in C++.** A modified C++ client still cannot read another hospital’s data, because the server checks the signed `hospital` claim on the ID token. The C++ app only *displays* the claim.
4. **Disjoint ID ranges** (Hospital_A 110001–119999, Hospital_B 120001–129999). IDs can never clash with local patients, and an ID proves which hospital a record came from.
5. **Validation twice.** Python validates before upload; C++ validates again after download. Test TC-C5 proves the second layer works.
6. **Database first, memory second.** C++ structures are updated only after PostgreSQL commits, so the two can’t drift apart.
7. **Consent and provenance on shared records.** A shared record must say `source_hospital`, `shared_with` and `consent: true`, or it is rejected.

## 3. Firebase setup (console, ~20 minutes)

1. Create a Firebase project at console.firebase.google.com.
2. **Build → Realtime Database → Create database.** Pick a nearby location (e.g. Singapore, `asia-southeast1`) and start in **locked mode**.
3. **Rules tab:** paste `firebase/database.rules.json` and publish.
4. **Build → Authentication → Sign-in method:** enable **Email/Password**.
5. **Project settings → General:** copy the **Web API key** (add a Web app if none is shown).
6. **Project settings → Service accounts → Generate new private key.** Save the JSON **outside** the repository (e.g. `C:\secrets\`). This key bypasses all rules — anyone holding it controls the database. Never commit, email or screenshot it.
7. Copy `config/set_env.example.ps1` to `config/set_env.ps1`, fill it in and run `. .\config\set_env.ps1`.

## 4. Python: data and accounts

```powershell
pip install -r python/requirements.txt
python python/generate_hospital_datasets.py --with-faulty

python python/firebase_admin_tool.py setup-user --email hospital-a@smartcare.test --hospital Hospital_A
python python/firebase_admin_tool.py setup-user --email hospital-b@smartcare.test --hospital Hospital_B

python python/firebase_admin_tool.py upload --hospital Hospital_A --csv dataset/hospital_a_patients.csv --dry-run
python python/firebase_admin_tool.py upload --hospital Hospital_A --csv dataset/hospital_a_patients.csv
python python/firebase_admin_tool.py upload --hospital Hospital_B --csv dataset/hospital_b_patients.csv

python python/firebase_admin_tool.py share --from Hospital_A --to Hospital_B --patient-id 110005 --consent
```

Check the Firebase console: `SmartCare/Hospital_A/Patients`, `SmartCare/Hospital_B/Patients` and `SmartCare/Shared_Records/Hospital_B/P110005` should exist. Take screenshots for the report.

## 5. PostgreSQL migration

1. Export a snapshot with your existing PowerShell script (rollback point).
2. Run the **STEP 0** pre-check queries at the top of `database/phase3_migration.sql` by hand. Fix or delete any rows they return.
3. Run the migration:
   ```powershell
   & "C:\Program Files\PostgreSQL\18\bin\psql.exe" -h localhost -U postgres -d smartcare -v ON_ERROR_STOP=1 -f database\phase3_migration.sql
   ```

## 6. C++ integration

Copy `phase2.cpp` to `phase3.cpp` (keep Phase 2 intact for the record), then:

```cpp
// near the top, after the existing #includes
#include "cloud/cloud_menu.h"
```

In `main()`, after the data structures are created:

```cpp
smartcare::FirebaseClient firebase = smartcare::FirebaseClient::fromEnvironment();
```

Add a menu line and a branch:

```cpp
cout << "\n20. Cloud Data Exchange (Firebase)";
...
else if (choice == 20)
{
    smartcare::cloudMenu(firebase, db, patients, patientHash, actions);
}
```

Compile (C++17 is required):

```powershell
g++ -std=c++17 -O2 -Wall phase3.cpp -o smartcare.exe        # MinGW
cl /std:c++17 /EHsc /W4 phase3.cpp                           # MSVC
```

Run the automated tests the same way:

```powershell
g++ -std=c++17 -Wall -I cloud tests/test_cloud_import.cpp -o test_cloud.exe
.\test_cloud.exe
```

## 7. Fixes needed in the existing phase2.cpp (Phase 3, part A)

These came from reading `phase2.cpp`. Several of them would make the Phase 3 test checklist fail as the code stands now.

**A1. Queues are not persistent (fails “records remain after restart”).** On startup only patients and doctors are loaded. Appointments and emergencies exist in PostgreSQL but the C++ queues start empty, and “Process” only pops from memory, so the database never learns the case was handled. The migration adds a `status` column. Then:

- On startup, load waiting rows with the same `\copy` pattern you use for patients: `SELECT appointment_id, patient_id, doctor_id FROM appointments WHERE status='WAITING' ORDER BY appointment_id`.
- When processing an appointment (FIFO = lowest ID):
  `UPDATE appointments SET status='DONE' WHERE appointment_id = (SELECT MIN(appointment_id) FROM appointments WHERE status='WAITING');`
- When processing an emergency, use the same ordering as the heap (see A2):
  `UPDATE emergency SET status='DONE' WHERE emergency_id = (SELECT emergency_id FROM emergency WHERE status='WAITING' ORDER BY priority, emergency_id LIMIT 1);`

**A2. Equal emergency priorities are not handled fairly.** `std::priority_queue` is not stable: two “Critical” cases may come out in any order. Add an arrival number as a tie-breaker so earlier arrivals go first. Then the heap and the SQL `ORDER BY priority, emergency_id` agree.

```cpp
class Emergency {
public:
    int patientID, priority;
    long long seq;                 // arrival order: emergency_id when loaded, counter for new
    Emergency(int p, int pr, long long s) : patientID(p), priority(pr), seq(s) {}
    // ... display(), getPriorityName() unchanged
};

struct CompareEmergency {
    bool operator()(const Emergency &a, const Emergency &b) const {
        if (a.priority != b.priority) return a.priority > b.priority; // lower number = more urgent
        return a.seq > b.seq;                                         // then first come, first served
    }
};
```

Initialise the counter to (largest loaded `emergency_id` + 1) and increment it for each new case.

**A3. Memory is updated before the database.** In Add Patient, `patients.push_back(p)` and `patientHash.emplace` run before `db.execute`. If the insert fails, the app shows a patient that PostgreSQL does not have. Run `db.execute` first; push into the structures only if it returns `true`. Same for doctors and appointments.

**A4. `patientExists` is O(n) even though you have a hash map.** It walks the linked list. Use `patientHash.count(id) > 0` for O(1) average lookup. This is a good point to make about DSA in the viva.

**A5. Unchecked numeric input.** Only the menu choice is checked. Typing a letter for ID or age leaves `cin` in a failed state and stores garbage. Use one helper everywhere:

```cpp
int readInt(const string &prompt, int lo, int hi) {
    int v;
    while (true) {
        cout << prompt;
        if (cin >> v && v >= lo && v <= hi) return v;
        cout << "Enter a whole number between " << lo << " and " << hi << ".\n";
        clearInput();
    }
}
// e.g. age = readInt("Enter Age: ", 0, 120);  priority = readInt("Priority (1-5): ", 1, 5);
```

Also normalise gender with `toupper` and accept only M/F/O. The migration’s CHECK constraints now reject anything else.

**A6. Password in a public repository.** `_putenv("PGPASSWORD=admin")` is visible on GitHub. Remove that line, set `PGPASSWORD` in `config/set_env.ps1` (or use `%APPDATA%\postgresql\pgpass.conf`), and **change the postgres password**. Rewriting Git history does not un-publish a value that was already public.

**A7. CSV loader splits on commas.** A name containing a comma (e.g. `"Rawat, Jr"`) would break `loadPatientsFromDatabase`. The synthetic data has none, so this is low priority. Mention it as a known limitation, or parse quoted CSV fields properly.

**A8. Process-per-query cost.** Every `db.execute` starts a new `psql.exe`, which is roughly 50–150 ms each. That is fine for a prototype. If the large-dataset test feels slow, measure it and report the number rather than hiding it. libpq is the real fix, but not needed for Phase 3.

Doctors are not imported from the cloud in this package. Adding `validateDoctors` follows exactly the same pattern as `validatePatients`. It is a good exercise for a team member to own and explain.

## 8. Demo script (≈8 minutes)

1. Show the Firebase console: Hospital_A, Hospital_B, Shared_Records/Hospital_B. Show the Rules tab.
2. Start SmartCare and open menu 20. Sign in as **hospital-b**; the status line shows `[Hospital_B]`.
3. Option 3: import Hospital_B patients. Show the validation report, approve, then confirm in pgAdmin:
   `SELECT source, COUNT(*) FROM patients GROUP BY source;`
4. Option 2: try to read Hospital_A. Expected: **ACCESS DENIED (HTTP 401)**. This is the access-control evidence.
5. Option 4: import records shared with Hospital_B. P110005 is imported from Hospital_A.
6. Run option 3 again. Every record is now **SKIPPED**, so the import is idempotent.
7. Upload the faulty file with `--skip-validation`, import again, and show the rejections. Option 5 shows the audit log.
8. Restart SmartCare and search for 110005. It is still there (persistence).

## 9. Test cases (record actual results + screenshots)

| ID | Test | Expected |
|---|---|---|
| TC-C1 | Sign in with wrong password | “INVALID_LOGIN_CREDENTIALS”, no token |
| TC-C2 | Hospital_B reads own dataset | 50 received, 50 approved, imported |
| TC-C3 | Hospital_B reads Hospital_A | HTTP 401, nothing imported, logged in Recent Actions |
| TC-C4 | Import same dataset twice | Second run: all SKIPPED, row count unchanged |
| TC-C5 | Faulty dataset (`--skip-validation`) | 8 rejected with reasons, valid rows imported |
| TC-C6 | Shared record with `consent:false` | Rejected: “no recorded consent” |
| TC-C7 | Internet off during retrieval | “Cloud request failed …”, PostgreSQL unchanged, app keeps running |
| TC-C8 | PostgreSQL stopped during import | “transaction rolled back”, in-memory list unchanged |
| TC-C9 | Python upload of faulty CSV without flag | Refused, “8 invalid row(s)… Nothing uploaded” |
| TC-A1 | Book 3 appointments, restart, process | Processed in booking order; DB status = DONE |
| TC-A2 | Two priority-1 emergencies | Earlier one processed first (after fix A2) |
| TC-A3 | Letters entered for age | Re-prompted, no garbage stored (after fix A5) |
| TC-A4 | Add patient while DB is down | Error shown, patient **not** in memory (after fix A3) |
| TC-A5 | Snapshot → restore into `smartcare_restore` DB | Row counts match the original |

## 10. How to measure success (for the report)

- **Correctness:** all TC rows pass. `tests/test_cloud_import.cpp` passes 53/53.
- **Security:** cross-hospital read denied (TC-C3). No secrets in Git (`git log -p | findstr /i "PGPASSWORD private_key"` returns nothing new).
- **Data quality:** 100% of injected faults rejected (TC-C5), and each rejection has a stated reason in `cloud_import_log`.
- **Reliability:** network and DB failures leave the system consistent (TC-C7, TC-C8).
- **Performance:** time the import of 50 and 500 records and report the numbers honestly.

## 11. Honest limitations to state in the report

- The data is synthetic. A real deployment would need consent records, audit trails, encryption at rest, and compliance with India’s DPDP Act 2023. None of these are claimed here.
- The ID token is passed as a URL parameter on a local process command line. That is acceptable for a prototype; production would use libcurl.
- `tests/mock_firebase_server.py` is only a test double. **Mark Firebase “completed” only after the demo runs against the real Firebase project.**
- Shared-record access depends on the admin tool. There is no hospital-to-hospital request/approval workflow in the app.
