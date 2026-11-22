-- ============================================================
-- SmartCare Phase 3 migration
-- Run once against the existing `smartcare` database:
--   psql -h localhost -U postgres -d smartcare -v ON_ERROR_STOP=1 -f database/phase3_migration.sql
--
-- Safe to re-run (IF NOT EXISTS / guarded constraint creation).
-- Take a snapshot first (scripts/ export) so you can roll back.
-- ============================================================

-- ------------------------------------------------------------
-- STEP 0: Pre-checks. If any query below returns rows, fix or delete
-- those rows BEFORE running the rest; otherwise the constraints fail.
-- (Run this block on its own first and read the output.)
-- ------------------------------------------------------------
-- Orphan appointments (patient/doctor that no longer exists):
--   SELECT a.* FROM appointments a LEFT JOIN patients p USING (patient_id) WHERE p.patient_id IS NULL;
--   SELECT a.* FROM appointments a LEFT JOIN doctors d  USING (doctor_id)  WHERE d.doctor_id  IS NULL;
-- Orphan emergencies / medical records:
--   SELECT e.* FROM emergency e LEFT JOIN patients p USING (patient_id) WHERE p.patient_id IS NULL;
--   SELECT m.* FROM medical_records m LEFT JOIN patients p USING (patient_id) WHERE p.patient_id IS NULL;
-- Out-of-range values:
--   SELECT * FROM patients  WHERE age NOT BETWEEN 0 AND 120 OR gender NOT IN ('M','F','O');
--   SELECT * FROM emergency WHERE priority NOT BETWEEN 1 AND 5;

BEGIN;

-- ------------------------------------------------------------
-- 1. Provenance: where did each patient row come from?
-- ------------------------------------------------------------
ALTER TABLE patients ADD COLUMN IF NOT EXISTS source VARCHAR(40) NOT NULL DEFAULT 'LOCAL';
ALTER TABLE patients ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT now();

-- ------------------------------------------------------------
-- 2. Queue persistence: appointments and emergencies need a status so
--    the C++ queues can be rebuilt after a restart, and "Process"
--    can be saved instead of only popping from memory.
-- ------------------------------------------------------------
ALTER TABLE appointments ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'WAITING';
ALTER TABLE appointments ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT now();
ALTER TABLE emergency    ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'WAITING';
ALTER TABLE emergency    ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT now();

-- ------------------------------------------------------------
-- 3. Integrity constraints (guarded so the script can be re-run)
-- ------------------------------------------------------------
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'patients_age_chk') THEN
        ALTER TABLE patients ADD CONSTRAINT patients_age_chk CHECK (age BETWEEN 0 AND 120);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'patients_gender_chk') THEN
        ALTER TABLE patients ADD CONSTRAINT patients_gender_chk CHECK (gender IN ('M','F','O'));
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'emergency_priority_chk') THEN
        ALTER TABLE emergency ADD CONSTRAINT emergency_priority_chk CHECK (priority BETWEEN 1 AND 5);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'appointments_status_chk') THEN
        ALTER TABLE appointments ADD CONSTRAINT appointments_status_chk
            CHECK (status IN ('WAITING','DONE','CANCELLED'));
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'emergency_status_chk') THEN
        ALTER TABLE emergency ADD CONSTRAINT emergency_status_chk
            CHECK (status IN ('WAITING','DONE'));
    END IF;

    -- Foreign keys: a booking/emergency/record must refer to a real patient/doctor.
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'appointments_patient_fk') THEN
        ALTER TABLE appointments ADD CONSTRAINT appointments_patient_fk
            FOREIGN KEY (patient_id) REFERENCES patients(patient_id);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'appointments_doctor_fk') THEN
        ALTER TABLE appointments ADD CONSTRAINT appointments_doctor_fk
            FOREIGN KEY (doctor_id) REFERENCES doctors(doctor_id);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'emergency_patient_fk') THEN
        ALTER TABLE emergency ADD CONSTRAINT emergency_patient_fk
            FOREIGN KEY (patient_id) REFERENCES patients(patient_id);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'medical_records_patient_fk') THEN
        ALTER TABLE medical_records ADD CONSTRAINT medical_records_patient_fk
            FOREIGN KEY (patient_id) REFERENCES patients(patient_id);
    END IF;
END $$;

-- Speeds up "load waiting queue on startup" queries.
CREATE INDEX IF NOT EXISTS appointments_waiting_idx ON appointments (appointment_id) WHERE status = 'WAITING';
CREATE INDEX IF NOT EXISTS emergency_waiting_idx    ON emergency (priority, emergency_id) WHERE status = 'WAITING';

-- ------------------------------------------------------------
-- 4. Cloud import audit log (evidence for the Phase 3 demo/report)
-- ------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cloud_import_log (
    log_id      SERIAL PRIMARY KEY,
    imported_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    cloud_path  VARCHAR(200) NOT NULL,
    record_key  VARCHAR(60)  NOT NULL,
    patient_id  INTEGER,
    status      VARCHAR(10)  NOT NULL CHECK (status IN ('IMPORTED','SKIPPED','REJECTED')),
    reason      VARCHAR(200)
);

COMMIT;

-- ------------------------------------------------------------
-- Useful verification queries for the demo / report
-- ------------------------------------------------------------
-- SELECT source, COUNT(*) FROM patients GROUP BY source ORDER BY source;
-- SELECT status, COUNT(*) FROM cloud_import_log GROUP BY status;
-- SELECT * FROM cloud_import_log ORDER BY log_id DESC LIMIT 20;
