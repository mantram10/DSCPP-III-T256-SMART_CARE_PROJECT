-- ============================================================
-- SmartCare Phase 3 - query benchmark
-- Runs the same queries the C++ app and website use, with EXPLAIN ANALYZE.
-- Look at the last line of each block: "Execution Time: X ms"
-- and whether it says "Seq Scan" (reads whole table) or "Index Scan".
--
--   psql -h localhost -U postgres -d smartcare_test -f database/benchmark_queries.sql > results_before.txt
-- then run performance_indexes.sql and repeat into results_after.txt
-- ============================================================
\echo '--- Q1 Search patient by ID (primary key) ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT * FROM patients WHERE patient_id = 12345;

\echo '--- Q2 Search patient by name (contains) ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT * FROM patients WHERE LOWER(name) LIKE '%rawat%' ORDER BY patient_id LIMIT 100;

\echo '--- Q3 Next appointment in queue (FIFO front) ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT appointment_id, patient_id, doctor_id FROM appointments
WHERE status = 'WAITING' ORDER BY appointment_id LIMIT 1;

\echo '--- Q4 Most urgent emergency (heap top) ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT emergency_id, patient_id, priority FROM emergency
WHERE status = 'WAITING' ORDER BY priority, emergency_id LIMIT 1;

\echo '--- Q5 Medical records of one patient ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT * FROM medical_records WHERE patient_id = 12345 ORDER BY record_id;

\echo '--- Q6 Appointments of one patient (patient page) ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT * FROM appointments WHERE patient_id = 12345 ORDER BY appointment_id;

\echo '--- Q7 Delete check: does this doctor have appointments? ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT COUNT(*) FROM appointments WHERE doctor_id = 500;

\echo '--- Q8 Startup load of waiting queue (C++) ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT appointment_id, patient_id, doctor_id FROM appointments
WHERE status = 'WAITING' ORDER BY appointment_id;

\echo '--- Q9 Dashboard: patients by source ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT source, COUNT(*) FROM patients GROUP BY source;

\echo '--- Q10 Website page 500 of patients (OFFSET paging) ---'
EXPLAIN (ANALYZE, COSTS OFF) SELECT * FROM patients ORDER BY patient_id LIMIT 100 OFFSET 49900;
