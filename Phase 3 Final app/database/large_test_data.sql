-- ============================================================
-- SmartCare Phase 3 - LARGE TEST DATASET (synthetic, reproducible)
--
-- Runs ONLY on a database whose name contains "test" (e.g. smartcare_test),
-- so your real smartcare data can never be overwritten.
--
-- Default size (about 400,000 rows):
--   99,999 patients | 1,000 doctors | 200,000 appointments (2,000 waiting)
--   50,000 emergencies (1,000 waiting) | 150,000 medical records
--
-- Run:
--   psql -h localhost -U postgres -d smartcare_test -v ON_ERROR_STOP=1 -f database/large_test_data.sql
-- Smaller / larger run (any value can be overridden):
--   psql ... -v patients=10000 -v appointments=20000 -f database/large_test_data.sql
-- ============================================================
\timing on

-- ---------- size settings (override with -v name=value) ----------
\if :{?patients}      \else \set patients 99999       \endif
\if :{?doctors}       \else \set doctors 1000         \endif
\if :{?appointments}  \else \set appointments 200000  \endif
\if :{?waiting_appts} \else \set waiting_appts 2000   \endif
\if :{?emergencies}   \else \set emergencies 50000    \endif
\if :{?waiting_emerg} \else \set waiting_emerg 1000   \endif
\if :{?records}       \else \set records 150000       \endif

-- ---------- safety: never touch the real database ----------
DO $$
BEGIN
    IF current_database() NOT LIKE '%test%' THEN
        RAISE EXCEPTION 'Refusing to load test data into "%". Create and use smartcare_test.', current_database();
    END IF;
END $$;

-- ---------- same tables as phase3.cpp createTables() ----------
CREATE TABLE IF NOT EXISTS patients (patient_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL,
    age INTEGER, gender CHAR(1), source VARCHAR(40) NOT NULL DEFAULT 'LOCAL');
CREATE TABLE IF NOT EXISTS doctors (doctor_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL,
    specialization VARCHAR(100));
CREATE TABLE IF NOT EXISTS appointments (appointment_id SERIAL PRIMARY KEY, patient_id INTEGER,
    doctor_id INTEGER, status VARCHAR(10) NOT NULL DEFAULT 'WAITING');
CREATE TABLE IF NOT EXISTS emergency (emergency_id SERIAL PRIMARY KEY, patient_id INTEGER,
    priority INTEGER, status VARCHAR(10) NOT NULL DEFAULT 'WAITING');
CREATE TABLE IF NOT EXISTS medical_records (record_id SERIAL PRIMARY KEY, patient_id INTEGER,
    disease VARCHAR(200), treatment VARCHAR(300), medicine VARCHAR(300));
CREATE TABLE IF NOT EXISTS cloud_import_log (log_id SERIAL PRIMARY KEY,
    imported_at TIMESTAMPTZ NOT NULL DEFAULT now(), cloud_path VARCHAR(200) NOT NULL,
    record_key VARCHAR(60) NOT NULL, patient_id INTEGER, status VARCHAR(10) NOT NULL
    CHECK (status IN ('IMPORTED','SKIPPED','REJECTED')), reason VARCHAR(200));

-- Start clean each run (test database only - checked above)
TRUNCATE medical_records, emergency, appointments, patients, doctors, cloud_import_log RESTART IDENTITY;

SELECT setseed(0.42);  -- same data every run, so results are repeatable

-- ---------- doctors ----------
INSERT INTO doctors (doctor_id, name, specialization)
SELECT i,
       'Dr ' || (ARRAY['Aarav','Vivaan','Aditya','Arjun','Kabir','Ananya','Diya','Priya','Sneha','Kavya',
                       'Meera','Rohan','Ishaan','Neha','Tanvi','Rahul'])[1 + floor(random() * 16)::int]
       || ' ' || (ARRAY['Sharma','Verma','Negi','Rawat','Bisht','Joshi','Gupta','Singh','Thapa','Panwar',
                        'Chauhan','Kumar','Mehta','Nautiyal','Bhatt','Pant'])[1 + floor(random() * 16)::int],
       (ARRAY['Cardiology','Neurology','Orthopaedics','Paediatrics','Dermatology','General Medicine',
              'ENT','Ophthalmology','Gynaecology','Psychiatry','Oncology','Pulmonology'])[1 + floor(random() * 12)::int]
FROM generate_series(1, :doctors) AS g(i);

-- ---------- patients (local IDs 1-99999; 110001+ stay reserved for cloud hospitals) ----------
INSERT INTO patients (patient_id, name, age, gender, source)
SELECT i,
       (ARRAY['Aarav','Vivaan','Aditya','Arjun','Rohan','Kabir','Ishaan','Rahul','Vikram','Karan',
              'Ananya','Diya','Priya','Sneha','Kavya','Isha','Meera','Pooja','Riya','Neha'])[1 + floor(random() * 20)::int]
       || ' ' || (ARRAY['Sharma','Verma','Negi','Rawat','Bisht','Joshi','Gupta','Singh','Thapa','Panwar',
                        'Chauhan','Kumar','Mehta','Nautiyal','Bhatt','Pant'])[1 + floor(random() * 16)::int],
       floor(random() * 96)::int,
       CASE WHEN random() < 0.02 THEN 'O' WHEN random() < 0.5 THEN 'F' ELSE 'M' END,
       'LOCAL'
FROM generate_series(1, LEAST(:patients, 99999)) AS g(i);

-- ---------- appointments: newest rows are the waiting queue ----------
INSERT INTO appointments (patient_id, doctor_id, status)
SELECT 1 + floor(random() * LEAST(:patients, 99999))::int,
       1 + floor(random() * :doctors)::int,
       CASE WHEN i > :appointments - :waiting_appts THEN 'WAITING'
            WHEN i % 33 = 0 THEN 'CANCELLED' ELSE 'DONE' END
FROM generate_series(1, :appointments) AS g(i);

-- ---------- emergencies: more low-urgency than critical, like a real ED ----------
INSERT INTO emergency (patient_id, priority, status)
SELECT 1 + floor(random() * LEAST(:patients, 99999))::int,
       (ARRAY[1,2,2,3,3,3,4,4,4,4,5,5])[1 + floor(random() * 12)::int],
       CASE WHEN i > :emergencies - :waiting_emerg THEN 'WAITING' ELSE 'DONE' END
FROM generate_series(1, :emergencies) AS g(i);

-- ---------- medical records (disease, treatment and medicine kept consistent) ----------
INSERT INTO medical_records (patient_id, disease, treatment, medicine)
SELECT 1 + floor(random() * LEAST(:patients, 99999))::int,
       (ARRAY['Viral fever','Type 2 diabetes','Hypertension','Asthma','Migraine','Fracture','Gastritis',
              'Dengue','Typhoid','Anaemia'])[k],
       (ARRAY['Rest and fluids','Diet control and monitoring','Lifestyle changes','Inhaler therapy',
              'Pain management','Cast and physiotherapy','Diet and antacids','Fluids and monitoring',
              'Antibiotic course','Iron supplementation'])[k],
       (ARRAY['Paracetamol','Metformin','Amlodipine','Salbutamol','Sumatriptan','Ibuprofen','Pantoprazole',
              'Paracetamol','Azithromycin','Ferrous sulphate'])[k]
FROM (SELECT 1 + floor(random() * 10)::int AS k FROM generate_series(1, :records)) AS g;

ANALYZE;  -- refresh planner statistics after bulk load

\timing off
\echo
\echo '===== smartcare_test loaded ====='
SELECT 'patients' AS table_name, COUNT(*) AS total_rows FROM patients
UNION ALL SELECT 'doctors', COUNT(*) FROM doctors
UNION ALL SELECT 'appointments (all)', COUNT(*) FROM appointments
UNION ALL SELECT 'appointments WAITING', COUNT(*) FROM appointments WHERE status = 'WAITING'
UNION ALL SELECT 'emergency (all)', COUNT(*) FROM emergency
UNION ALL SELECT 'emergency WAITING', COUNT(*) FROM emergency WHERE status = 'WAITING'
UNION ALL SELECT 'medical_records', COUNT(*) FROM medical_records;
SELECT pg_size_pretty(pg_database_size(current_database())) AS database_size;
