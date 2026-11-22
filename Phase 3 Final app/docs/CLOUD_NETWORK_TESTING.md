# SmartCare Phase 3 — Cloud Network Testing (5 hospitals)

Hospitals A–E each have their own Firebase login, their own patient dataset and their own
"shared records" inbox. Firebase security rules (enforced on Google's server) decide who may
read, share and revoke. This guide sets up the network and runs an automated test of every
hospital pair.

| Hospital | Login | Patient IDs |
|---|---|---|
| Hospital_A | hospital-a@smartcare.test | 110001–119999 |
| Hospital_B | hospital-b@smartcare.test | 120001–129999 |
| Hospital_C | hospital-c@smartcare.test | 130001–139999 |
| Hospital_D | hospital-d@smartcare.test | 140001–149999 |
| Hospital_E | hospital-e@smartcare.test | 150001–159999 |

## 1. Set up the network (once, ~5 minutes)

In PowerShell, in the project folder, after `. .\config\set_env.ps1`:

```powershell
python python/generate_hospital_datasets.py          # 50 patients for each of A-E
python python/make_firebase_rules.py                 # rules for all 5 hospitals
```

Publish the rules: Firebase Console → Realtime Database → Rules → paste
`firebase/database.rules.json` → **Publish**.

```powershell
python python/firebase_admin_tool.py setup-network   # 5 logins, one password (asked once)
python python/firebase_admin_tool.py upload-all      # 5 datasets
python python/firebase_admin_tool.py status          # check: 50 patients each
```

`setup-network` sets a new password for hospital-a and hospital-b too; use it from now on.

## 2. Run the automated network test

```powershell
python tests/cloud_network_test.py
```

Enter the network password. The test takes about a minute and prints six sections:
read matrix, inbox matrix, full-mesh sharing, attacks, revoke, concurrency.
It writes **tests/cloud_network_report.md** with tables you can paste into your report.

Expected: **150/150 checks passed**. Any `!!` in a matrix or `SECURITY PROBLEM` line means a
rule is wrong — usually the rules were not published.

The test creates 20 temporary shares (patient 45 of each hospital) and removes them at the end.

## 3. Fill every inbox with shares (for demos)

```powershell
python python/firebase_admin_tool.py seed-shares --per-pair 2    # 40 consented shares
python python/firebase_admin_tool.py status                      # who received what
python python/firebase_admin_tool.py clear-shares                # remove all shares
```

## 4. Manual demo in the apps

C++ (`run_smartcare.bat`) → 20 → 1 → sign in as `hospital-c@smartcare.test`:

| Choice | Result |
|---|---|
| 3 | Imports Hospital_C's own 50 patients |
| 2 → Hospital_A | **ACCESS DENIED** |
| 6 → Hospital_E → 130010 → y | Shared with Hospital_E |
| 1 → hospital-e@… → 4 → y | Hospital_E receives P130010 (and any seeded shares from A, B, D) |

Website: Cloud Exchange → sign in as any hospital → the dataset list shows all five hospitals;
only your own is allowed.

Note: on one laptop all hospitals import into the same PostgreSQL database, so a patient already
imported once shows as SKIPPED for the next hospital. In a real network each hospital has its own
database.

## 5. Adding a 6th hospital

1. Add one line to `HOSPITAL_ID_RANGES` in `python/validation.py` and to `hospitalRanges()` in
   `cloud/cloud_import.h` (e.g. `Hospital_F`, 160001–169999).
2. Run `make_firebase_rules.py` and publish; run `generate_hospital_datasets.py`,
   `setup-network` and `upload-all`; recompile `phase3.exe`.
3. Re-run `cloud_network_test.py` — it automatically tests 6 × 6.
