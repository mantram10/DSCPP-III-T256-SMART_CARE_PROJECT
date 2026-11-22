"""
SmartCare Web - Phase 3 local web application.

Run (from the SmartCare_Phase3 folder):   web\\run_web.bat
or:                                        python web/app.py
Then open http://127.0.0.1:5000

Same PostgreSQL database and the same rules as phase3.exe:
  * Appointments: FIFO queue (oldest WAITING first)
  * Emergencies:  min-heap order (priority 1 first, then earliest arrival)
  * Cloud:        Firebase sign-in, validation, consent-based sharing
Adds full CRUD (create, read, update, delete) required by the Phase 3 plan.
"""
import os
import secrets
from collections import deque

from werkzeug.exceptions import HTTPException
from flask import Flask, abort, flash, redirect, render_template, request, session, url_for

import cloud

app = Flask(__name__)
app.secret_key = os.environ.get("SMARTCARE_WEB_SECRET") or secrets.token_hex(32)

DB = None                       # set in create_app()
ACTIONS = deque(maxlen=100)     # Stack of recent actions (newest first), like stack<string> in C++
CLOUD_SESSIONS = {}             # browser session id -> Firebase session (kept server-side)

LOCAL_ID_MAX = 99999            # 110001+ are reserved for cloud hospital IDs
PRIORITY_NAMES = {1: "Critical", 2: "Very Serious", 3: "Serious", 4: "Moderate", 5: "Low"}

# Same hospital graph as phase3.cpp
NETWORK_EDGES = [("City Hospital", "Apollo Hospital"), ("Apollo Hospital", "Fortis Hospital"),
                 ("Fortis Hospital", "Max Hospital"), ("City Hospital", "AIIMS")]


def create_app(database):
    global DB
    DB = database
    DB.ensure_schema()
    return app


# =================================================================== helpers
class FormError(Exception):
    pass


def push_action(text):
    ACTIONS.appendleft(text)


def form_int(name, label, lo, hi, required=True):
    raw = request.form.get(name, "").strip()
    if not raw and not required:
        return None
    try:
        value = int(raw)
    except ValueError:
        raise FormError(f"{label} must be a whole number.")
    if not lo <= value <= hi:
        raise FormError(f"{label} must be between {lo} and {hi}.")
    return value


def form_text(name, label, max_len, required=True):
    value = " ".join(request.form.get(name, "").split())  # trim + collapse spaces
    if required and not value:
        raise FormError(f"{label} cannot be empty.")
    if len(value) > max_len:
        raise FormError(f"{label} is too long (max {max_len} characters).")
    if any(ord(c) < 32 for c in value):
        raise FormError(f"{label} contains invalid characters.")
    return value


def form_gender():
    g = request.form.get("gender", "").strip().upper()
    if g not in ("M", "F", "O"):
        raise FormError("Gender must be M, F or O.")
    return g


def count(sql, params=()):
    row = DB.one(sql, params)
    return list(row.values())[0] if row else 0


PER_PAGE = 100


def paged(sql, params=(), arg="page"):
    """Run sql (ending in ORDER BY) one page at a time, so 100,000-row tables stay fast."""
    raw = request.args.get(arg, "1")
    page = int(raw) if raw.isdigit() and int(raw) > 0 else 1
    total = count(f"SELECT COUNT(*) AS n FROM ({sql}) sub", params)
    pages = max(1, -(-total // PER_PAGE))
    page = min(page, pages)
    rows, cols = DB.query(sql + " LIMIT %s OFFSET %s", list(params) + [PER_PAGE, (page - 1) * PER_PAGE])
    first = (page - 1) * PER_PAGE + 1 if total else 0
    info = {"page": page, "pages": pages, "total": total, "first": first,
            "last": first + len(rows) - 1 if rows else 0, "arg": arg}
    return rows, cols, info


def patient_exists(pid):
    return DB.one("SELECT patient_id FROM patients WHERE patient_id = %s", (pid,)) is not None


def doctor_exists(did):
    return DB.one("SELECT doctor_id FROM doctors WHERE doctor_id = %s", (did,)) is not None


def cloud_session():
    sid = session.get("sid")
    sess = CLOUD_SESSIONS.get(sid) if sid else None
    return sess if cloud.session_valid(sess) else None


# ================================================================ security
@app.before_request
def csrf_protect():
    if "csrf" not in session:
        session["csrf"] = secrets.token_hex(16)
        session["sid"] = secrets.token_hex(16)
    if request.method == "POST" and request.form.get("csrf") != session["csrf"]:
        abort(400, "Invalid form token. Reload the page and try again.")


@app.context_processor
def inject_globals():
    sess = cloud_session()
    def page_url(arg, n):
        args = request.args.to_dict()
        args[arg] = n
        return url_for(request.endpoint, **(request.view_args or {}), **args)
    return {"csrf": session.get("csrf", ""), "cloud_user": sess, "priority_names": PRIORITY_NAMES,
            "page_url": page_url}


@app.errorhandler(FormError)
def handle_form_error(e):
    flash(str(e), "error")
    return redirect(request.referrer or url_for("dashboard"))


@app.errorhandler(Exception)
def handle_db_error(e):
    if isinstance(e, HTTPException):  # 400/404 etc. keep their normal response
        return e
    if DB is not None and isinstance(e, DB.Error):
        app.logger.exception("Database error")
        return render_template("error.html", message=f"Database error: {e}"), 500
    raise e


# =============================================================== dashboard
@app.route("/")
def dashboard():
    stats = {
        "patients": count("SELECT COUNT(*) AS n FROM patients"),
        "doctors": count("SELECT COUNT(*) AS n FROM doctors"),
        "appointments": count("SELECT COUNT(*) AS n FROM appointments WHERE status = 'WAITING'"),
        "emergencies": count("SELECT COUNT(*) AS n FROM emergency WHERE status = 'WAITING'"),
        "records": count("SELECT COUNT(*) AS n FROM medical_records"),
    }
    by_source, _ = DB.query("SELECT source, COUNT(*) AS total FROM patients GROUP BY source ORDER BY source")
    next_appt = DB.one(
        "SELECT a.appointment_id, a.patient_id, p.name AS patient_name, a.doctor_id, d.name AS doctor_name "
        "FROM appointments a LEFT JOIN patients p ON p.patient_id = a.patient_id "
        "LEFT JOIN doctors d ON d.doctor_id = a.doctor_id "
        "WHERE a.status = 'WAITING' ORDER BY a.appointment_id LIMIT 1")
    next_emerg = DB.one(
        "SELECT e.emergency_id, e.patient_id, p.name AS patient_name, e.priority FROM emergency e "
        "LEFT JOIN patients p ON p.patient_id = e.patient_id "
        "WHERE e.status = 'WAITING' ORDER BY e.priority, e.emergency_id LIMIT 1")
    return render_template("dashboard.html", stats=stats, by_source=by_source, next_appt=next_appt,
                           next_emerg=next_emerg, actions=list(ACTIONS)[:8])


# ================================================================ patients
@app.route("/patients")
def patients():
    q = request.args.get("q", "").strip()
    source = request.args.get("source", "").strip()
    sql, params, where = "SELECT * FROM patients", [], []
    if q:
        if q.isdigit():
            where.append("patient_id = %s")
            params.append(int(q))
        else:
            where.append("LOWER(name) LIKE %s")
            params.append(f"%{q.lower()}%")
    if source:
        where.append("source = %s")
        params.append(source)
    if where:
        sql += " WHERE " + " AND ".join(where)
    rows, cols, info = paged(sql + " ORDER BY patient_id", params)
    sources, _ = DB.query("SELECT DISTINCT source FROM patients ORDER BY source")
    return render_template("patients.html", rows=rows, cols=cols, info=info, q=q, source=source,
                           sources=[s["source"] for s in sources])


@app.route("/patients/add", methods=["POST"])
def patient_add():
    pid = form_int("patient_id", "Patient ID", 1, LOCAL_ID_MAX)
    name = form_text("name", "Name", 100)
    age = form_int("age", "Age", 0, 120)
    gender = form_gender()
    if patient_exists(pid):
        raise FormError(f"Patient {pid} already exists.")
    DB.execute("INSERT INTO patients (patient_id, name, age, gender) VALUES (%s, %s, %s, %s)",
               (pid, name, age, gender))
    push_action(f"Added Patient: {name}")
    flash(f"Patient {pid} ({name}) added.", "ok")
    return redirect(url_for("patients"))


@app.route("/patients/<int:pid>")
def patient_detail(pid):
    p_rows, p_cols = DB.query("SELECT * FROM patients WHERE patient_id = %s", (pid,))
    if not p_rows:
        abort(404)
    appts, appt_cols = DB.query(
        "SELECT a.*, d.name AS doctor_name FROM appointments a LEFT JOIN doctors d ON d.doctor_id = a.doctor_id "
        "WHERE a.patient_id = %s ORDER BY a.appointment_id", (pid,))
    emerg, emerg_cols = DB.query("SELECT * FROM emergency WHERE patient_id = %s ORDER BY emergency_id", (pid,))
    recs, rec_cols = DB.query("SELECT * FROM medical_records WHERE patient_id = %s ORDER BY record_id", (pid,))
    return render_template("patient_detail.html", p=p_rows[0], p_cols=p_cols, appts=appts, appt_cols=appt_cols,
                           emerg=emerg, emerg_cols=emerg_cols, recs=recs, rec_cols=rec_cols)


@app.route("/patients/<int:pid>/edit", methods=["GET", "POST"])
def patient_edit(pid):
    p = DB.one("SELECT * FROM patients WHERE patient_id = %s", (pid,))
    if not p:
        abort(404)
    if request.method == "POST":
        name = form_text("name", "Name", 100)
        age = form_int("age", "Age", 0, 120)
        gender = form_gender()
        DB.execute("UPDATE patients SET name = %s, age = %s, gender = %s WHERE patient_id = %s",
                   (name, age, gender, pid))
        push_action(f"Updated Patient {pid}")
        flash(f"Patient {pid} updated.", "ok")
        return redirect(url_for("patient_detail", pid=pid))
    return render_template("patient_edit.html", p=p)


@app.route("/patients/<int:pid>/delete", methods=["POST"])
def patient_delete(pid):
    refs = {
        "appointments": count("SELECT COUNT(*) AS n FROM appointments WHERE patient_id = %s", (pid,)),
        "emergency cases": count("SELECT COUNT(*) AS n FROM emergency WHERE patient_id = %s", (pid,)),
        "medical records": count("SELECT COUNT(*) AS n FROM medical_records WHERE patient_id = %s", (pid,)),
    }
    used = ", ".join(f"{n} {k}" for k, n in refs.items() if n)
    if used:
        raise FormError(f"Cannot delete patient {pid}: they still have {used}. Remove those first.")
    if DB.execute("DELETE FROM patients WHERE patient_id = %s", (pid,)):
        push_action(f"Deleted Patient {pid}")
        flash(f"Patient {pid} deleted.", "ok")
    return redirect(url_for("patients"))


# ================================================================= doctors
@app.route("/doctors")
def doctors():
    q = request.args.get("q", "").strip()
    sql, params = "SELECT * FROM doctors", []
    if q:
        if q.isdigit():
            sql += " WHERE doctor_id = %s"
            params.append(int(q))
        else:
            sql += " WHERE LOWER(name) LIKE %s OR LOWER(specialization) LIKE %s"
            params += [f"%{q.lower()}%"] * 2
    rows, cols, info = paged(sql + " ORDER BY doctor_id", params)
    return render_template("doctors.html", rows=rows, cols=cols, info=info, q=q)


@app.route("/doctors/add", methods=["POST"])
def doctor_add():
    did = form_int("doctor_id", "Doctor ID", 1, LOCAL_ID_MAX)
    name = form_text("name", "Name", 100)
    spec = form_text("specialization", "Specialization", 100)
    if doctor_exists(did):
        raise FormError(f"Doctor {did} already exists.")
    DB.execute("INSERT INTO doctors (doctor_id, name, specialization) VALUES (%s, %s, %s)", (did, name, spec))
    push_action(f"Added Doctor: {name}")
    flash(f"Doctor {did} ({name}) added.", "ok")
    return redirect(url_for("doctors"))


@app.route("/doctors/<int:did>/edit", methods=["GET", "POST"])
def doctor_edit(did):
    d = DB.one("SELECT * FROM doctors WHERE doctor_id = %s", (did,))
    if not d:
        abort(404)
    if request.method == "POST":
        name = form_text("name", "Name", 100)
        spec = form_text("specialization", "Specialization", 100)
        DB.execute("UPDATE doctors SET name = %s, specialization = %s WHERE doctor_id = %s", (name, spec, did))
        push_action(f"Updated Doctor {did}")
        flash(f"Doctor {did} updated.", "ok")
        return redirect(url_for("doctors"))
    return render_template("doctor_edit.html", d=d)


@app.route("/doctors/<int:did>/delete", methods=["POST"])
def doctor_delete(did):
    n = count("SELECT COUNT(*) AS n FROM appointments WHERE doctor_id = %s", (did,))
    if n:
        raise FormError(f"Cannot delete doctor {did}: they have {n} appointment(s).")
    if DB.execute("DELETE FROM doctors WHERE doctor_id = %s", (did,)):
        push_action(f"Deleted Doctor {did}")
        flash(f"Doctor {did} deleted.", "ok")
    return redirect(url_for("doctors"))


# ============================================================ appointments
@app.route("/appointments")
def appointments():
    base = ("SELECT a.*, p.name AS patient_name, d.name AS doctor_name, d.specialization "
            "FROM appointments a LEFT JOIN patients p ON p.patient_id = a.patient_id "
            "LEFT JOIN doctors d ON d.doctor_id = a.doctor_id ")
    waiting, cols, winfo = paged(base + "WHERE a.status = 'WAITING' ORDER BY a.appointment_id", (), "page")
    history, hcols, hinfo = paged(base + "WHERE a.status <> 'WAITING' ORDER BY a.appointment_id DESC", (), "hpage")
    return render_template("appointments.html", waiting=waiting, cols=cols, winfo=winfo,
                           history=history, hcols=hcols, hinfo=hinfo)


@app.route("/appointments/book", methods=["POST"])
def appointment_book():
    pid = form_int("patient_id", "Patient ID", 1, 999999)
    did = form_int("doctor_id", "Doctor ID", 1, LOCAL_ID_MAX)
    if not patient_exists(pid):
        raise FormError(f"Patient {pid} does not exist.")
    if not doctor_exists(did):
        raise FormError(f"Doctor {did} does not exist.")
    DB.execute("INSERT INTO appointments (patient_id, doctor_id, status) VALUES (%s, %s, 'WAITING')", (pid, did))
    pos = count("SELECT COUNT(*) AS n FROM appointments WHERE status = 'WAITING'")
    push_action(f"Booked Appointment for Patient {pid}")
    flash(f"Appointment booked. Position in queue: {pos}.", "ok")
    return redirect(url_for("appointments"))


@app.route("/appointments/process", methods=["POST"])
def appointment_process():
    front = DB.one("SELECT appointment_id, patient_id, doctor_id FROM appointments "
                   "WHERE status = 'WAITING' ORDER BY appointment_id LIMIT 1")
    if not front:
        flash("No appointments waiting.", "info")
    elif DB.execute("UPDATE appointments SET status = 'DONE' WHERE appointment_id = %s AND status = 'WAITING'",
                    (front["appointment_id"],)):
        push_action(f"Processed Appointment #{front['appointment_id']}")
        flash(f"Processed appointment #{front['appointment_id']} (patient {front['patient_id']}, "
              f"doctor {front['doctor_id']}) - first in, first out.", "ok")
    return redirect(url_for("appointments"))


@app.route("/appointments/<int:aid>/cancel", methods=["POST"])
def appointment_cancel(aid):
    if DB.execute("UPDATE appointments SET status = 'CANCELLED' WHERE appointment_id = %s AND status = 'WAITING'",
                  (aid,)):
        push_action(f"Cancelled Appointment #{aid}")
        flash(f"Appointment #{aid} cancelled.", "ok")
    return redirect(url_for("appointments"))


# =============================================================== emergency
@app.route("/emergency")
def emergency():
    base = ("SELECT e.*, p.name AS patient_name, p.age FROM emergency e "
            "LEFT JOIN patients p ON p.patient_id = e.patient_id ")
    waiting, cols, winfo = paged(base + "WHERE e.status = 'WAITING' ORDER BY e.priority, e.emergency_id", (), "page")
    history, hcols, hinfo = paged(base + "WHERE e.status <> 'WAITING' ORDER BY e.emergency_id DESC", (), "hpage")
    return render_template("emergency.html", waiting=waiting, cols=cols, winfo=winfo,
                           history=history, hcols=hcols, hinfo=hinfo)


@app.route("/emergency/add", methods=["POST"])
def emergency_add():
    pid = form_int("patient_id", "Patient ID", 1, 999999)
    pr = form_int("priority", "Priority", 1, 5)
    if not patient_exists(pid):
        raise FormError(f"Patient {pid} does not exist.")
    DB.execute("INSERT INTO emergency (patient_id, priority, status) VALUES (%s, %s, 'WAITING')", (pid, pr))
    push_action(f"Added Emergency for Patient {pid}")
    flash(f"Emergency case added for patient {pid} ({PRIORITY_NAMES[pr]}).", "ok")
    return redirect(url_for("emergency"))


@app.route("/emergency/process", methods=["POST"])
def emergency_process():
    top = DB.one("SELECT emergency_id, patient_id, priority FROM emergency WHERE status = 'WAITING' "
                 "ORDER BY priority, emergency_id LIMIT 1")
    if not top:
        flash("No emergency cases waiting.", "info")
    elif DB.execute("UPDATE emergency SET status = 'DONE' WHERE emergency_id = %s AND status = 'WAITING'",
                    (top["emergency_id"],)):
        push_action(f"Processed Emergency #{top['emergency_id']}")
        flash(f"Processed emergency #{top['emergency_id']} (patient {top['patient_id']}, "
              f"{PRIORITY_NAMES.get(top['priority'], top['priority'])}) - most urgent first.", "ok")
    return redirect(url_for("emergency"))


# ========================================================= medical records
@app.route("/records")
def records():
    pid = request.args.get("patient_id", "").strip()
    sql = ("SELECT m.*, p.name AS patient_name FROM medical_records m "
           "LEFT JOIN patients p ON p.patient_id = m.patient_id")
    params = []
    if pid.isdigit():
        sql += " WHERE m.patient_id = %s"
        params.append(int(pid))
    rows, cols, info = paged(sql + " ORDER BY m.record_id", params)
    return render_template("records.html", rows=rows, cols=cols, info=info, pid=pid)


@app.route("/records/add", methods=["POST"])
def record_add():
    pid = form_int("patient_id", "Patient ID", 1, 999999)
    disease = form_text("disease", "Disease", 200)
    treatment = form_text("treatment", "Treatment", 300)
    medicine = form_text("medicine", "Medicine", 300, required=False)
    if not patient_exists(pid):
        raise FormError(f"Patient {pid} does not exist.")
    DB.execute("INSERT INTO medical_records (patient_id, disease, treatment, medicine) VALUES (%s, %s, %s, %s)",
               (pid, disease, treatment, medicine))
    push_action(f"Added Medical Record for Patient {pid}")
    flash(f"Medical record added for patient {pid}.", "ok")
    return redirect(request.referrer or url_for("records"))


@app.route("/records/<int:rid>/edit", methods=["GET", "POST"])
def record_edit(rid):
    r = DB.one("SELECT * FROM medical_records WHERE record_id = %s", (rid,))
    if not r:
        abort(404)
    if request.method == "POST":
        disease = form_text("disease", "Disease", 200)
        treatment = form_text("treatment", "Treatment", 300)
        medicine = form_text("medicine", "Medicine", 300, required=False)
        DB.execute("UPDATE medical_records SET disease = %s, treatment = %s, medicine = %s WHERE record_id = %s",
                   (disease, treatment, medicine, rid))
        push_action(f"Updated Medical Record #{rid}")
        flash(f"Medical record #{rid} updated.", "ok")
        return redirect(url_for("records"))
    return render_template("record_edit.html", r=r)


@app.route("/records/<int:rid>/delete", methods=["POST"])
def record_delete(rid):
    if DB.execute("DELETE FROM medical_records WHERE record_id = %s", (rid,)):
        push_action(f"Deleted Medical Record #{rid}")
        flash(f"Medical record #{rid} deleted.", "ok")
    return redirect(request.referrer or url_for("records"))


# ========================================================= hospital graph
def build_graph():
    graph = {}
    for a, b in NETWORK_EDGES:
        graph.setdefault(a, []).append(b)
        graph.setdefault(b, []).append(a)
    return graph


def bfs(graph, start):
    order, visited, q = [], {start}, deque([start])
    while q:
        cur = q.popleft()
        order.append(cur)
        for nxt in graph[cur]:
            if nxt not in visited:
                visited.add(nxt)
                q.append(nxt)
    return order


def dfs(graph, start):
    order, visited, stack = [], set(), [start]
    while stack:
        cur = stack.pop()
        if cur in visited:
            continue
        visited.add(cur)
        order.append(cur)
        for nxt in reversed(graph[cur]):  # same order as phase3.cpp
            if nxt not in visited:
                stack.append(nxt)
    return order


@app.route("/network")
def network():
    graph = build_graph()
    start = request.args.get("start", "")
    algo = request.args.get("algo", "")
    result = None
    if start:
        if start not in graph:
            flash("Hospital not found.", "error")
        else:
            algo = "dfs" if algo == "dfs" else "bfs"
            result = dfs(graph, start) if algo == "dfs" else bfs(graph, start)
            push_action(f"{algo.upper()} traversal from {start}")
    return render_template("network.html", graph=graph, edges=NETWORK_EDGES, start=start,
                           algo=algo or "bfs", result=result)


# ===================================================================== cloud
def cloud_paths():
    sess = cloud_session()
    paths = {h: (f"{cloud.ROOT}/{h}/Patients", "dataset", h) for h in cloud.HOSPITAL_ID_RANGES}
    if sess and sess.get("hospital"):
        paths["shared"] = (f"{cloud.ROOT}/Shared_Records/{sess['hospital']}", "shared", sess["hospital"])
    return paths


@app.route("/cloud")
def cloud_page():
    return render_template("cloud.html", problem=cloud.config_problem(), cfg=cloud.config(),
                           hospitals=list(cloud.HOSPITAL_ID_RANGES), ranges=cloud.HOSPITAL_ID_RANGES)


@app.route("/cloud/signin", methods=["POST"])
def cloud_signin():
    try:
        sess = cloud.sign_in(request.form.get("email", ""), request.form.get("password", ""))
    except cloud.CloudError as e:
        flash(str(e), "error")
        return redirect(url_for("cloud_page"))
    CLOUD_SESSIONS[session["sid"]] = sess
    push_action(f"Cloud sign-in: {sess['hospital'] or sess['email']}")
    if not sess["hospital"]:
        flash("Signed in, but this account has no 'hospital' claim. Run setup-user for it.", "error")
    else:
        flash(f"Signed in as {sess['email']} for {sess['hospital']}.", "ok")
    return redirect(url_for("cloud_page"))


@app.route("/cloud/signout", methods=["POST"])
def cloud_signout():
    CLOUD_SESSIONS.pop(session.get("sid"), None)
    flash("Signed out of the cloud.", "info")
    return redirect(url_for("cloud_page"))


def _validate_cloud(which):
    sess = cloud_session()
    if not sess:
        raise cloud.CloudError("Please sign in first.")
    paths = cloud_paths()
    if which not in paths:
        raise cloud.CloudError("Unknown cloud source.")
    path, mode, expected = paths[which]
    node = cloud.fetch(sess, path)
    rep = cloud.validate_records(node, mode, expected, patient_exists)
    return path, rep


def _log_statements(path, rep, include_accepted):
    stmts = []
    if include_accepted:
        for p in rep["accepted"]:
            stmts.append(("INSERT INTO patients (patient_id, name, age, gender, source) VALUES (%s, %s, %s, %s, %s) "
                          "ON CONFLICT (patient_id) DO NOTHING",
                          (p["patient_id"], p["name"], p["age"], p["gender"], f"FIREBASE:{p['source_hospital']}")))
            stmts.append(("INSERT INTO cloud_import_log (cloud_path, record_key, patient_id, status, reason) "
                          "VALUES (%s, %s, %s, 'IMPORTED', NULL)", (path, p["key"], p["patient_id"])))
    for i in rep["issues"]:
        stmts.append(("INSERT INTO cloud_import_log (cloud_path, record_key, patient_id, status, reason) "
                      "VALUES (%s, %s, %s, %s, %s)", (path, i["key"][:60], i["patient_id"], i["status"], i["reason"])))
    return stmts


@app.route("/cloud/preview", methods=["POST"])
def cloud_preview():
    which = request.form.get("which", "")
    try:
        path, rep = _validate_cloud(which)
    except cloud.CloudError as e:
        flash(str(e), "error")
        if e.status in (401, 403):
            push_action(f"Cloud access denied: {which}")
        return redirect(url_for("cloud_page"))
    if rep["fatal"]:
        flash("Cannot import: " + rep["fatal"], "error")
        return redirect(url_for("cloud_page"))
    return render_template("cloud_preview.html", which=which, path=path, rep=rep)


@app.route("/cloud/import", methods=["POST"])
def cloud_import():
    which = request.form.get("which", "")
    approve = request.form.get("approve") == "yes"
    try:
        path, rep = _validate_cloud(which)  # fetch again: import exactly what is in the cloud now
    except cloud.CloudError as e:
        flash(str(e), "error")
        return redirect(url_for("cloud_page"))
    stmts = _log_statements(path, rep, include_accepted=approve)
    if stmts:
        DB.transaction(stmts)  # one transaction: all rows + log, or nothing
    if approve and rep["accepted"]:
        push_action(f"Imported {len(rep['accepted'])} patient(s) from {path}")
        flash(f"{len(rep['accepted'])} record(s) imported into PostgreSQL from {path}.", "ok")
    else:
        flash("No records imported. Validation results were written to the import log.", "info")
    return redirect(url_for("cloud_log"))


@app.route("/cloud/share", methods=["POST"])
def cloud_share():
    sess = cloud_session()
    target = request.form.get("target", "")
    if request.form.get("consent") != "yes":
        push_action("Share refused (no consent)")
        flash("NOT shared: patient consent is required.", "error")
        return redirect(url_for("cloud_page"))
    try:
        pid = form_int("patient_id", "Patient ID", 1, 999999)
        body = cloud.share(sess, target, pid)
    except cloud.CloudError as e:
        flash(str(e), "error")
        return redirect(url_for("cloud_page"))
    push_action(f"Shared P{pid} with {target}")
    flash(f"SHARED P{pid} ({body['name']}) from {body['source_hospital']} to {target}.", "ok")
    return redirect(url_for("cloud_page"))


@app.route("/cloud/revoke", methods=["POST"])
def cloud_revoke():
    sess = cloud_session()
    target = request.form.get("target", "")
    try:
        pid = form_int("patient_id", "Patient ID", 1, 999999)
        cloud.revoke(sess, target, pid)
    except cloud.CloudError as e:
        flash(str(e), "error")
        return redirect(url_for("cloud_page"))
    push_action(f"Revoked P{pid} from {target}")
    flash(f"REVOKED P{pid}: {target} can no longer import it (copies already imported stay).", "ok")
    return redirect(url_for("cloud_page"))


@app.route("/cloud/log")
def cloud_log():
    rows, cols = DB.query("SELECT * FROM cloud_import_log ORDER BY log_id DESC LIMIT 200")
    summary, _ = DB.query("SELECT status, COUNT(*) AS total FROM cloud_import_log GROUP BY status ORDER BY status")
    return render_template("cloud_log.html", rows=rows, cols=cols, summary=summary)


@app.route("/actions")
def actions():
    return render_template("actions.html", actions=list(ACTIONS))


# ===================================================================== main
if __name__ == "__main__":
    from db import Database
    create_app(Database())
    print("SmartCare Web running at http://127.0.0.1:5000  (press Ctrl+C to stop)")
    app.run(host="127.0.0.1", port=int(os.environ.get("SMARTCARE_WEB_PORT", "5000")), debug=False)
