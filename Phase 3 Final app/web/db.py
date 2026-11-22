"""
SmartCare Web - database layer.

Uses the SAME PostgreSQL database and tables as phase3.exe, so the
web app and the C++ app always show the same data.

Connection settings (environment variables, with SmartCare defaults):
    PGHOST=localhost  PGPORT=5432  PGUSER=postgres  PGPASSWORD=admin  PGDATABASE=smartcare

SQL is written with %s placeholders and portable syntax. A small SQLite
adapter exists ONLY so the automated tests can run without PostgreSQL.
"""
import os
import re

# Same statements as createTables() in phase3.cpp (safe to run every start).
POSTGRES_SCHEMA = [
    "CREATE TABLE IF NOT EXISTS patients (patient_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL, "
    "age INTEGER, gender CHAR(1))",
    "CREATE TABLE IF NOT EXISTS doctors (doctor_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL, "
    "specialization VARCHAR(100))",
    "CREATE TABLE IF NOT EXISTS appointments (appointment_id SERIAL PRIMARY KEY, patient_id INTEGER, "
    "doctor_id INTEGER)",
    "CREATE TABLE IF NOT EXISTS emergency (emergency_id SERIAL PRIMARY KEY, patient_id INTEGER, priority INTEGER)",
    "CREATE TABLE IF NOT EXISTS medical_records (record_id SERIAL PRIMARY KEY, patient_id INTEGER, "
    "disease VARCHAR(200), treatment VARCHAR(300), medicine VARCHAR(300))",
    "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS disease VARCHAR(200)",
    "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS treatment VARCHAR(300)",
    "ALTER TABLE medical_records ADD COLUMN IF NOT EXISTS medicine VARCHAR(300)",
    "ALTER TABLE patients ADD COLUMN IF NOT EXISTS source VARCHAR(40) NOT NULL DEFAULT 'LOCAL'",
    "ALTER TABLE appointments ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'WAITING'",
    "ALTER TABLE emergency ADD COLUMN IF NOT EXISTS status VARCHAR(10) NOT NULL DEFAULT 'WAITING'",
    "CREATE TABLE IF NOT EXISTS cloud_import_log (log_id SERIAL PRIMARY KEY, "
    "imported_at TIMESTAMPTZ NOT NULL DEFAULT now(), cloud_path VARCHAR(200) NOT NULL, "
    "record_key VARCHAR(60) NOT NULL, patient_id INTEGER, status VARCHAR(10) NOT NULL "
    "CHECK (status IN ('IMPORTED','SKIPPED','REJECTED')), reason VARCHAR(200))",
]


class Database:
    """Opens one connection per call. Simple and safe for a local app."""

    def __init__(self):
        try:
            import psycopg
            from psycopg.rows import dict_row
        except ImportError as exc:
            raise SystemExit("psycopg is not installed: python -m pip install -r web/requirements.txt") from exc
        self._psycopg = psycopg
        self._dict_row = dict_row
        self.IntegrityError = psycopg.IntegrityError
        self.Error = psycopg.Error
        self.conninfo = dict(
            host=os.environ.get("PGHOST", "localhost"),
            port=os.environ.get("PGPORT", "5432"),
            user=os.environ.get("PGUSER", "postgres"),
            password=os.environ.get("PGPASSWORD") or "admin",
            dbname=os.environ.get("PGDATABASE", "smartcare"),
            connect_timeout=5,
        )

    def _connect(self):
        return self._psycopg.connect(**self.conninfo, row_factory=self._dict_row)

    def query(self, sql, params=()):
        """SELECT -> (rows as list of dicts, column names in table order)."""
        with self._connect() as conn, conn.cursor() as cur:
            cur.execute(sql, params)
            cols = [d.name for d in cur.description] if cur.description else []
            return cur.fetchall(), cols

    def one(self, sql, params=()):
        rows, _ = self.query(sql, params)
        return rows[0] if rows else None

    def execute(self, sql, params=()):
        """INSERT/UPDATE/DELETE in one transaction. Returns affected row count."""
        with self._connect() as conn, conn.cursor() as cur:
            cur.execute(sql, params)
            return cur.rowcount

    def transaction(self, statements):
        """Run [(sql, params), ...] atomically: all succeed or none do."""
        with self._connect() as conn, conn.cursor() as cur:
            for sql, params in statements:
                cur.execute(sql, params)

    def ensure_schema(self):
        for stmt in POSTGRES_SCHEMA:
            self.execute(stmt)


class SqliteDatabase(Database):
    """TEST ONLY: same interface on an in-memory SQLite database."""

    SCHEMA = [
        "CREATE TABLE patients (patient_id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER, "
        "gender TEXT, source TEXT NOT NULL DEFAULT 'LOCAL')",
        "CREATE TABLE doctors (doctor_id INTEGER PRIMARY KEY, name TEXT NOT NULL, specialization TEXT)",
        "CREATE TABLE appointments (appointment_id INTEGER PRIMARY KEY AUTOINCREMENT, patient_id INTEGER, "
        "doctor_id INTEGER, status TEXT NOT NULL DEFAULT 'WAITING')",
        "CREATE TABLE emergency (emergency_id INTEGER PRIMARY KEY AUTOINCREMENT, patient_id INTEGER, "
        "priority INTEGER, status TEXT NOT NULL DEFAULT 'WAITING')",
        "CREATE TABLE medical_records (record_id INTEGER PRIMARY KEY AUTOINCREMENT, patient_id INTEGER, "
        "disease TEXT, treatment TEXT, medicine TEXT)",
        "CREATE TABLE cloud_import_log (log_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, cloud_path TEXT NOT NULL, "
        "record_key TEXT NOT NULL, patient_id INTEGER, status TEXT NOT NULL, reason TEXT)",
    ]

    def __init__(self):
        import sqlite3
        self._sqlite3 = sqlite3
        self.IntegrityError = sqlite3.IntegrityError
        self.Error = sqlite3.Error
        self.conn = sqlite3.connect(":memory:", check_same_thread=False)
        self.conn.row_factory = sqlite3.Row

    @staticmethod
    def _sql(sql):
        return re.sub(r"%s", "?", sql)

    def query(self, sql, params=()):
        cur = self.conn.execute(self._sql(sql), params)
        cols = [d[0] for d in cur.description] if cur.description else []
        return [dict(r) for r in cur.fetchall()], cols

    def execute(self, sql, params=()):
        with self.conn:
            return self.conn.execute(self._sql(sql), params).rowcount

    def transaction(self, statements):
        with self.conn:
            for sql, params in statements:
                self.conn.execute(self._sql(sql), params)

    def ensure_schema(self):
        for stmt in self.SCHEMA:
            self.conn.execute(stmt)
