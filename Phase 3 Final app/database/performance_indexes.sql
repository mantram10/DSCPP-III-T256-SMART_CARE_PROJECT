-- ============================================================
-- SmartCare Phase 3 - performance indexes
-- Indexes the columns the apps filter on. Safe to re-run.
-- Measure with benchmark_queries.sql BEFORE and AFTER running this.
--   psql -h localhost -U postgres -d smartcare_test -f database/performance_indexes.sql
-- (Apply to the real smartcare database too once you have measured the benefit.)
-- ============================================================
\timing on
-- "Show records / appointments / emergencies of patient X" and delete checks
CREATE INDEX IF NOT EXISTS medical_records_patient_idx ON medical_records (patient_id);
CREATE INDEX IF NOT EXISTS appointments_patient_idx    ON appointments (patient_id);
CREATE INDEX IF NOT EXISTS appointments_doctor_idx     ON appointments (doctor_id);
CREATE INDEX IF NOT EXISTS emergency_patient_idx       ON emergency (patient_id);
-- Queue front / heap top: small partial indexes over WAITING rows only
CREATE INDEX IF NOT EXISTS appointments_waiting_idx ON appointments (appointment_id) WHERE status = 'WAITING';
CREATE INDEX IF NOT EXISTS emergency_waiting_idx    ON emergency (priority, emergency_id) WHERE status = 'WAITING';
ANALYZE;
\timing off
\echo 'Indexes created.'
