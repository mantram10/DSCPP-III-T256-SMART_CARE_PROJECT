# SmartCare Phase 3 — Large Database Testing

Goal: show that SmartCare stays correct and responsive with ~400,000 rows, measure where time goes,
and show the effect of an optimisation (indexes). All tests use a **separate database, `smartcare_test`**.
The real `smartcare` database is never touched (the loader refuses to run on it).

## 1. Create and fill the test database

```powershell
cd C:\Users\Mantram\Downloads\SmartCare_Phase3
$env:PGPASSWORD = "admin"
$psql = "C:\Program Files\PostgreSQL\18\bin\psql.exe"
& $psql -h localhost -U postgres -d postgres -c "CREATE DATABASE smartcare_test"
& $psql -h localhost -U postgres -d smartcare_test -v ON_ERROR_STOP=1 -f database\large_test_data.sql
```

Default size: 99,999 patients, 1,000 doctors, 200,000 appointments (2,000 waiting),
50,000 emergencies (1,000 waiting), 150,000 medical records. Change any of them with
`-v patients=20000` etc. The same seed gives the same data every run.

## 2. Query benchmark: before and after indexes

```powershell
& $psql -h localhost -U postgres -d smartcare_test -f database\benchmark_queries.sql > tests\results_before.txt
& $psql -h localhost -U postgres -d smartcare_test -f database\performance_indexes.sql
& $psql -h localhost -U postgres -d smartcare_test -f database\benchmark_queries.sql > tests\results_after.txt
```

Open both files. For each query, copy the `Execution Time` and note `Seq Scan` vs `Index Scan`.

## 3. C++ application timing

```powershell
powershell -ExecutionPolicy Bypass -File tests\benchmark_cpp.ps1
```

Also run `run_smartcare_test.bat` once by hand and check that startup shows the large counts.

## 4. Website timing

1. Double-click `web\run_web_test.bat` (website on smartcare_test).
2. In PowerShell: `python tests\benchmark_web.py`

## 5. Large cloud import (optional)

```powershell
python python/generate_hospital_datasets.py --count 5000 --out dataset_large
python python/firebase_admin_tool.py upload --hospital Hospital_B --csv dataset_large/hospital_b_patients.csv
```

Run `run_smartcare_test.bat` → 20 → 1 (Hospital B) → 3 → `y`. Time it with a stopwatch.
Then restore the normal cloud data:
`python python/firebase_admin_tool.py upload --hospital Hospital_B --csv dataset/hospital_b_patients.csv`

## 6. Correctness checks at scale

| Check | How | Expected |
|---|---|---|
| FIFO with 2,000 waiting | Process appointment 3 times | IDs come out in increasing order |
| Heap with 1,000 waiting | Process emergency 3 times | Priority 1 first; equal priority in arrival order |
| Hash lookup | Search patient 54321 | Found instantly |
| Duplicate protection | Import the same 5,000 cloud records twice | Second run: all SKIPPED |
| Web paging | Patients → Last | “Showing 99,901–99,999 of 99,999” |
| Restart persistence | Close and reopen the C++ app | Same waiting counts as before closing |

## 7. Results table (fill in for the report)

| Test | Rows | Before indexes | After indexes |
|---|---|---|---|
| Q1 patient by ID | 99,999 | ms | ms |
| Q5 records of one patient | 150,000 | ms | ms |
| Q6 appointments of one patient | 200,000 | ms | ms |
| Q7 doctor delete check | 200,000 | ms | ms |
| Q3 queue front | 200,000 | ms | ms |
| Q4 heap top | 50,000 | ms | ms |
| C++ startup (load all) | ~100,000 patients | s | — |
| C++ display all patients | 99,999 | s | — |
| Web: patient detail page | — | ms | ms |
| Cloud import 5,000 records | 5,000 | s | — |

## 8. What to write in the analysis

- Which operations stayed fast as data grew, and why (primary-key index, hash map, paging).
- Which got slow before indexing (full table scans on `patient_id` / `doctor_id` columns) and how much the
  indexes helped, with your measured numbers.
- Known limits, stated honestly: the C++ app loads every patient into memory at startup (fine at 100,000;
  would need paging at millions); “Display all patients” prints every row; name search with `LIKE '%x%'`
  cannot use a normal index (a `pg_trgm` index would fix that).
