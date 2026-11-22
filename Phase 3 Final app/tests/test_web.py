"""
SmartCare Web - automated tests (no PostgreSQL needed: uses the SQLite test adapter).

    python tests/test_web.py                       # web pages, CRUD, queues
    (mock Firebase running on :8765) python tests/test_web.py --cloud
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "web"))

CLOUD = "--cloud" in sys.argv
if CLOUD:
    os.environ.update(SMARTCARE_FIREBASE_DB_URL="http://127.0.0.1:8765",
                      SMARTCARE_FIREBASE_AUTH_URL="http://127.0.0.1:8765",
                      SMARTCARE_FIREBASE_API_KEY="test-key",
                      SMARTCARE_HOSPITAL_EMAIL="b@smartcare.test")

import app as webapp  # noqa: E402
from db import SqliteDatabase  # noqa: E402

checks = failures = 0


def check(cond, label):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("  FAIL:", label)


db = SqliteDatabase()
flask_app = webapp.create_app(db)
flask_app.testing = True
c = flask_app.test_client()


def csrf():
    c.get("/")
    with c.session_transaction() as s:
        return s["csrf"]


def post(url, **data):
    data["csrf"] = csrf()
    return c.post(url, data=data, follow_redirects=True)


def text(resp):
    return resp.get_data(as_text=True)


print("[pages]")
for url in ["/", "/patients", "/doctors", "/appointments", "/emergency", "/records", "/network",
            "/network?start=City+Hospital&algo=bfs", "/network?start=City+Hospital&algo=dfs",
            "/cloud", "/cloud/log", "/actions"]:
    r = c.get(url)
    check(r.status_code == 200, f"GET {url} -> {r.status_code}")

print("[csrf]")
check(c.post("/patients/add", data={"patient_id": 1}).status_code == 400, "POST without token rejected")

print("[patients CRUD]")
check("added" in text(post("/patients/add", patient_id=1, name="  Rawat,  Jr ", age=30, gender="m")), "add patient")
r = text(c.get("/patients"))
check("Rawat, Jr" in r and "Male" in r and "LOCAL" in r, "list shows all fields, trimmed name")
check("already exists" in text(post("/patients/add", patient_id=1, name="X", age=1, gender="F")), "duplicate ID")
check("between 0 and 120" in text(post("/patients/add", patient_id=2, name="X", age=150, gender="F")), "age range")
check("whole number" in text(post("/patients/add", patient_id="abc", name="X", age=1, gender="F")), "non-numeric id")
check("Gender must be" in text(post("/patients/add", patient_id=2, name="X", age=1, gender="Z")), "gender")
check("cannot be empty" in text(post("/patients/add", patient_id=2, name="   ", age=1, gender="F")), "empty name")
check("between 1 and 99999" in text(post("/patients/add", patient_id=110001, name="X", age=1, gender="F")), "cloud range reserved")
post("/patients/add", patient_id=2, name="Priya O'Neil", age=25, gender="F")
post("/patients/add", patient_id=3, name="Delete Me", age=40, gender="O")
check("Priya O&#39;Neil" in text(c.get("/patients?q=neil")), "search by name (quote safe)")
check("Delete Me" not in text(c.get("/patients?q=2")), "search by exact id")
post("/patients/2/edit", name="Priya Sharma", age=26, gender="F")
check(db.one("SELECT name, age FROM patients WHERE patient_id = %s", (2,)) == {"name": "Priya Sharma", "age": 26}, "edit patient")
check("deleted" in text(post("/patients/3/delete")), "delete patient")
check(c.get("/patients/999").status_code == 404, "missing patient 404")
xss = "<script>alert(1)</script>"
post("/patients/add", patient_id=4, name=xss, age=1, gender="M")
check(xss not in text(c.get("/patients")), "names are HTML-escaped")

print("[doctors CRUD]")
post("/doctors/add", doctor_id=10, name="Dr Mehta", specialization="Cardiology")
post("/doctors/add", doctor_id=11, name="Dr Bisht", specialization="Neurology")
check("Cardiology" in text(c.get("/doctors?q=cardio")), "search specialization")
post("/doctors/11/edit", name="Dr Bisht", specialization="Orthopaedics")
check(db.one("SELECT specialization FROM doctors WHERE doctor_id = %s", (11,))["specialization"] == "Orthopaedics", "edit doctor")

print("[appointments FIFO]")
for pid in (1, 2, 1):
    post("/appointments/book", patient_id=pid, doctor_id=10)
check("does not exist" in text(post("/appointments/book", patient_id=999, doctor_id=10)), "unknown patient")
r = text(post("/appointments/process"))
check("Processed appointment #1" in r, "first booked processed first")
r = text(post("/appointments/process"))
check("Processed appointment #2" in r, "then second")
post("/appointments/3/cancel")
check(db.one("SELECT status FROM appointments WHERE appointment_id = %s", (3,))["status"] == "CANCELLED", "cancel")
check("No appointments waiting" in text(post("/appointments/process")), "empty queue")
check("they have" in text(post("/doctors/10/delete")), "doctor with appointments protected")

print("[emergency heap order]")
post("/emergency/add", patient_id=1, priority=3)
post("/emergency/add", patient_id=2, priority=1)   # id 2
post("/emergency/add", patient_id=1, priority=1)   # id 3, same priority, later
r = text(c.get("/emergency"))
check('class="row-priority-1"' in r, "triage row colouring")
check("Processed emergency #2" in text(post("/emergency/process")), "priority 1 first")
check("Processed emergency #3" in text(post("/emergency/process")), "equal priority: earlier arrival first")
check("Processed emergency #1" in text(post("/emergency/process")), "then priority 3")
check("between 1 and 5" in text(post("/emergency/add", patient_id=1, priority=9)), "priority range")

print("[medical records CRUD]")
post("/records/add", patient_id=1, disease="Fever", treatment="Rest", medicine="")
post("/records/add", patient_id=2, disease="Fracture", treatment="Cast", medicine="Ibuprofen")
r = text(c.get("/records"))
check("Fever" in r and "Ibuprofen" in r and "Patient Name" in r, "records list with all fields + patient name")
check("Fracture" not in text(c.get("/records?patient_id=1")), "filter by patient")
post("/records/1/edit", disease="Viral fever", treatment="Rest", medicine="Paracetamol")
check(db.one("SELECT medicine FROM medical_records WHERE record_id = %s", (1,))["medicine"] == "Paracetamol", "edit record")
check("still have" in text(post("/patients/1/delete")), "patient with history protected")
r = text(c.get("/patients/1"))
check("Viral fever" in r and "Appointments" in r and "Emergency cases" in r, "patient detail shows history")
post("/records/2/delete")
check(db.one("SELECT COUNT(*) AS n FROM medical_records")["n"] == 1, "delete record")

print("[graph]")
r = text(c.get("/network?start=City+Hospital&algo=bfs"))
check("City Hospital → Apollo Hospital → AIIMS → Fortis Hospital → Max Hospital → END" in r, "BFS order matches C++")
r = text(c.get("/network?start=City+Hospital&algo=dfs"))
check("City Hospital → Apollo Hospital → Fortis Hospital → Max Hospital → AIIMS → END" in r, "DFS order matches C++")

print("[dashboard + actions]")
r = text(c.get("/"))
check("Most urgent emergency" in r and "Recent actions" in r, "dashboard")
check("Deleted Medical Record #2" in text(c.get("/actions")), "actions stack newest first")

print("[pagination with bulk data]")
db.transaction([("INSERT INTO patients (patient_id, name, age, gender) VALUES (%s, %s, %s, %s)",
                 (50000 + i, f"Bulk Patient {i}", i % 100, "MFO"[i % 3])) for i in range(1, 251)])
total = db.one("SELECT COUNT(*) AS n FROM patients")["n"]
r = text(c.get("/patients"))
check(f"Showing 1–100 of {total}" in r, "page 1 shows 100 rows")
check(r.count("<tr>") + r.count('<tr class') <= 101, "only one page of rows rendered")
r = text(c.get("/patients?page=3"))
check(f"Showing 201–" in r and "Page 3 of 3" in r, "page 3")
check("Page 3 of 3" in text(c.get("/patients?page=999")), "page number clamped")
check("Page 1 of 1" not in text(c.get("/patients?q=Bulk")) and "Showing 1–100 of 250" in text(c.get("/patients?q=Bulk")), "search is paged")
check("q=Bulk" in text(c.get("/patients?q=Bulk")).replace("&amp;", "&"), "next link keeps the search")

if CLOUD:
    print("[cloud]")
    check("Sign-in failed" in text(post("/cloud/signin", email="b@smartcare.test", password="bad")), "wrong password")
    check("for Hospital_B" in text(post("/cloud/signin", email="  ", password="test123")), "default email sign-in")
    r = text(post("/cloud/preview", which="Hospital_A"))
    check("ACCESS DENIED" in r, "B cannot read A")
    r = text(post("/cloud/preview", which="Hospital_B"))
    m = re.search(r"<b>(\d+)</b><span>Approved", r)
    check(m and m.group(1) == "5", "faulty dataset: 5 approved, 8 rejected")
    check("name is empty" in r and "gender must be M, F or O" in r, "rejection reasons shown")
    r = text(post("/cloud/import", which="Hospital_B", approve="yes"))
    check("5 record(s) imported" in r and "IMPORTED" in r and "REJECTED" in r, "import + log")
    check(db.one("SELECT COUNT(*) AS n FROM patients WHERE source = 'FIREBASE:Hospital_B'")["n"] == 5, "rows in DB")
    r = text(post("/cloud/preview", which="Hospital_B"))
    check("patient_id already exists" in r, "second run skipped")

    post("/cloud/signin", email="a@smartcare.test", password="test123")
    check("consent is required" in text(post("/cloud/share", target="Hospital_B", patient_id=110020)), "no consent")
    check("SHARED P110020" in text(post("/cloud/share", target="Hospital_B", patient_id=110020, consent="yes")), "share")
    check("only share patients of Hospital_A" in text(post("/cloud/share", target="Hospital_B", patient_id=120001, consent="yes")), "own patients only")

    post("/cloud/signin", email="b@smartcare.test", password="test123")
    check("DENIED" in text(post("/cloud/revoke", target="Hospital_A", patient_id=110020)), "B cannot revoke A's share")
    r = text(post("/cloud/preview", which="shared"))
    check("110020" in r, "B sees shared record")
    post("/cloud/import", which="shared", approve="yes")
    check(db.one("SELECT source FROM patients WHERE patient_id = %s", (110020,))["source"] == "FIREBASE:Hospital_A", "shared import")

    post("/cloud/signin", email="a@smartcare.test", password="test123")
    check("REVOKED P110020" in text(post("/cloud/revoke", target="Hospital_B", patient_id=110020)), "revoke")

print(f"\n{checks - failures}/{checks} checks passed")
sys.exit(1 if failures else 0)
