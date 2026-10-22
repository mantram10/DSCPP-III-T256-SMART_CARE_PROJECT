import os
from flask import Flask, jsonify, request, send_from_directory
import psycopg2
from psycopg2.extras import RealDictCursor

app = Flask(__name__, static_folder="static")

DB_CONFIG = {
    "host": os.getenv("SMARTCARE_DB_HOST", "localhost"),
    "port": os.getenv("SMARTCARE_DB_PORT", "5432"),
    "dbname": os.getenv("SMARTCARE_DB_NAME", "smartcare"),
    "user": os.getenv("SMARTCARE_DB_USER", "postgres"),
    "password": os.getenv("SMARTCARE_DB_PASSWORD", ""),
}

def db():
    return psycopg2.connect(**DB_CONFIG)

def query(sql, params=(), fetch=True):
    conn = db()
    try:
        with conn.cursor(cursor_factory=RealDictCursor) as cur:
            cur.execute(sql, params)
            result = cur.fetchall() if fetch and cur.description else []
        if not fetch:
            conn.commit()
        else:
            conn.commit()
        return [dict(r) for r in result]
    except Exception:
        conn.rollback()
        raise
    finally:
        conn.close()

def ensure_tables():
    # Uses the same table names/columns as the Phase 2 repository.
    statements = [
        """CREATE TABLE IF NOT EXISTS patients (
            patient_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL,
            age INTEGER, gender CHAR(1))""",
        """CREATE TABLE IF NOT EXISTS doctors (
            doctor_id INTEGER PRIMARY KEY, name VARCHAR(100) NOT NULL,
            specialization VARCHAR(100))""",
        """CREATE TABLE IF NOT EXISTS appointments (
            appointment_id SERIAL PRIMARY KEY, patient_id INTEGER, doctor_id INTEGER)""",
        """CREATE TABLE IF NOT EXISTS emergency (
            emergency_id SERIAL PRIMARY KEY, patient_id INTEGER, priority INTEGER)""",
        """CREATE TABLE IF NOT EXISTS medical_records (
            record_id SERIAL PRIMARY KEY, patient_id INTEGER,
            diagnosis VARCHAR(200), prescription VARCHAR(300))"""
    ]
    conn = db()
    try:
        with conn.cursor() as cur:
            for sql in statements:
                cur.execute(sql)
        conn.commit()
    finally:
        conn.close()

@app.route("/")
def index():
    return send_from_directory("static", "index.html")

@app.route("/api/health")
def health():
    try:
        rows = query("SELECT current_database() AS database, current_user AS db_user")
        return jsonify(ok=True, **rows[0])
    except Exception as e:
        return jsonify(ok=False, error=str(e)), 503

@app.route("/api/dashboard")
def dashboard():
    try:
        result = {}
        for key, table in [("patients","patients"),("doctors","doctors"),("appointments","appointments"),("emergencies","emergency"),("medical_records","medical_records")]:
            result[key] = query(f"SELECT COUNT(*)::int AS n FROM {table}")[0]["n"]
        result["database"] = DB_CONFIG["dbname"]
        return jsonify(result)
    except Exception as e:
        return jsonify(error=str(e)), 503

@app.route("/api/patients", methods=["GET","POST"])
def patients():
    try:
        if request.method == "GET":
            return jsonify(query("SELECT patient_id, name, age, gender FROM patients ORDER BY patient_id"))
        d = request.get_json(force=True)
        query("INSERT INTO patients(patient_id,name,age,gender) VALUES(%s,%s,%s,%s)",
              (int(d["patient_id"]), d["name"].strip(), int(d["age"]), (d.get("gender") or "U")[:1].upper()), False)
        return jsonify(ok=True, message="Patient saved to PostgreSQL"), 201
    except Exception as e:
        return jsonify(error=str(e)), 400

@app.route("/api/doctors", methods=["GET","POST"])
def doctors():
    try:
        if request.method == "GET":
            return jsonify(query("SELECT doctor_id, name, specialization FROM doctors ORDER BY doctor_id"))
        d=request.get_json(force=True)
        query("INSERT INTO doctors(doctor_id,name,specialization) VALUES(%s,%s,%s)",
              (int(d["doctor_id"]),d["name"].strip(),d.get("specialization","").strip()),False)
        return jsonify(ok=True,message="Doctor saved to PostgreSQL"),201
    except Exception as e:
        return jsonify(error=str(e)),400

@app.route("/api/appointments", methods=["GET","POST"])
def appointments():
    try:
        if request.method=="GET":
            return jsonify(query("""SELECT a.appointment_id,a.patient_id,p.name AS patient_name,
                a.doctor_id,d.name AS doctor_name FROM appointments a
                LEFT JOIN patients p ON p.patient_id=a.patient_id
                LEFT JOIN doctors d ON d.doctor_id=a.doctor_id ORDER BY a.appointment_id DESC"""))
        d=request.get_json(force=True)
        query("INSERT INTO appointments(patient_id,doctor_id) VALUES(%s,%s)",
              (int(d["patient_id"]),int(d["doctor_id"])),False)
        return jsonify(ok=True,message="Appointment saved to PostgreSQL"),201
    except Exception as e:
        return jsonify(error=str(e)),400

@app.route("/api/emergencies", methods=["GET","POST"])
def emergencies():
    try:
        if request.method=="GET":
            return jsonify(query("""SELECT e.emergency_id,e.patient_id,p.name AS patient_name,e.priority
                FROM emergency e LEFT JOIN patients p ON p.patient_id=e.patient_id
                ORDER BY e.priority ASC,e.emergency_id ASC"""))
        d=request.get_json(force=True)
        priority=int(d["priority"])
        if priority not in range(1,6): return jsonify(error="Priority must be between 1 and 5"),400
        query("INSERT INTO emergency(patient_id,priority) VALUES(%s,%s)",
              (int(d["patient_id"]),priority),False)
        return jsonify(ok=True,message="Emergency case saved to PostgreSQL"),201
    except Exception as e:
        return jsonify(error=str(e)),400

@app.route("/api/medical-records", methods=["GET","POST"])
def medical_records():
    try:
        if request.method=="GET":
            return jsonify(query("""SELECT m.record_id,m.patient_id,p.name AS patient_name,m.diagnosis,m.prescription
                FROM medical_records m LEFT JOIN patients p ON p.patient_id=m.patient_id
                ORDER BY m.record_id DESC"""))
        d=request.get_json(force=True)
        query("INSERT INTO medical_records(patient_id,diagnosis,prescription) VALUES(%s,%s,%s)",
              (int(d["patient_id"]),d["diagnosis"].strip(),d.get("prescription","").strip()),False)
        return jsonify(ok=True,message="Medical record saved to PostgreSQL"),201
    except Exception as e:
        return jsonify(error=str(e)),400

if __name__ == "__main__":
    try:
        ensure_tables()
        print("SmartCare API starting at http://127.0.0.1:5000")
        print("Database:", DB_CONFIG["dbname"], "@", DB_CONFIG["host"], "port", DB_CONFIG["port"])
        app.run(host="127.0.0.1", port=5000, debug=False)
    except Exception as e:
        print("\nCould not start SmartCare.")
        print("Check PostgreSQL is running and SMARTCARE_DB_PASSWORD is configured.")
        print("Details:", e)
        input("Press Enter to close...")
