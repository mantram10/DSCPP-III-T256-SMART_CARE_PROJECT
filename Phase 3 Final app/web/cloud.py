"""
SmartCare Web - Firebase cloud exchange.

Same behaviour as cloud/*.h in the C++ app:
  * sign in with a hospital account (Firebase Auth REST) -> ID token
  * read / share / revoke through the Realtime Database REST API
  * the Firebase security rules decide what each hospital may do
  * every downloaded record is re-validated before import

Only the Python standard library is used (urllib), so nothing extra
needs installing for the cloud part.
"""
import base64
import datetime as dt
import json
import os
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "python"))
from validation import HOSPITAL_ID_RANGES, validate_patient  # noqa: E402  (shared with uploader)

ROOT = "SmartCare"


class CloudError(Exception):
    def __init__(self, message, status=0):
        super().__init__(message)
        self.status = status


def _safe_base(url):
    return url.startswith("https://") or url.startswith("http://127.0.0.1:") or url.startswith("http://localhost:")


def config():
    return {
        "db_url": os.environ.get("SMARTCARE_FIREBASE_DB_URL", "").rstrip("/"),
        "api_key": os.environ.get("SMARTCARE_FIREBASE_API_KEY", ""),
        "auth_url": (os.environ.get("SMARTCARE_FIREBASE_AUTH_URL") or "https://identitytoolkit.googleapis.com").rstrip("/"),
        "default_email": os.environ.get("SMARTCARE_HOSPITAL_EMAIL", ""),
    }


def config_problem():
    c = config()
    if not c["db_url"]:
        return "SMARTCARE_FIREBASE_DB_URL is not set (start the app with run_web.bat)."
    if not _safe_base(c["db_url"]) or not _safe_base(c["auth_url"]):
        return "Firebase URLs must use https://"
    if not c["api_key"]:
        return "SMARTCARE_FIREBASE_API_KEY is not set."
    return ""


def _http(method, url, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(url, data=data, method=method,
                                 headers={"Content-Type": "application/json"} if data else {})
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            raw = resp.read().decode()
            return resp.status, (json.loads(raw) if raw else None)
    except urllib.error.HTTPError as e:
        raw = e.read().decode(errors="replace")
        try:
            return e.code, json.loads(raw)
        except ValueError:
            return e.code, raw
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        raise CloudError(f"Network error: {getattr(e, 'reason', e)}. PostgreSQL data is unaffected.")


def _claims(token):
    try:
        payload = token.split(".")[1]
        payload += "=" * (-len(payload) % 4)
        return json.loads(base64.urlsafe_b64decode(payload))
    except (IndexError, ValueError):
        return {}


def sign_in(email, password):
    """Returns a session dict {token, email, hospital, exp}. Raises CloudError."""
    problem = config_problem()
    if problem:
        raise CloudError(problem)
    c = config()
    email = (email or "").strip() or c["default_email"]
    status, body = _http("POST", f"{c['auth_url']}/v1/accounts:signInWithPassword?key={c['api_key']}",
                         {"email": email, "password": password, "returnSecureToken": True})
    if status != 200:
        msg = body.get("error", {}).get("message", "unknown error") if isinstance(body, dict) else str(body)
        raise CloudError(f"Sign-in failed: {msg}", status)
    claims = _claims(body["idToken"])
    return {"token": body["idToken"], "email": email, "hospital": claims.get("hospital", ""),
            "exp": claims.get("exp", time.time() + 3600)}


def session_valid(sess):
    return bool(sess) and time.time() < sess.get("exp", 0) - 60


def _db(method, sess, path, body=None):
    if not session_valid(sess):
        raise CloudError("Not signed in, or the session expired. Please sign in again.")
    c = config()
    status, data = _http(method, f"{c['db_url']}/{path}.json?auth={sess['token']}", body)
    if status in (401, 403):
        raise CloudError(f"ACCESS DENIED by Firebase security rules (HTTP {status}) for {path}", status)
    if status >= 300:
        raise CloudError(f"Firebase returned HTTP {status}: {data}", status)
    return data


# ---------------------------------------------------------------- validation
def validate_records(node, mode, expected, exists_locally):
    """Mirror of validatePatients() in cloud/cloud_import.h.

    mode: 'dataset' (expected = hospital whose path was read)
          'shared'  (expected = receiving hospital)
    Returns dict(total, accepted=[...], issues=[...], fatal='').
    """
    rep = {"total": 0, "accepted": [], "issues": [], "fatal": ""}
    if node is None:
        return rep
    if isinstance(node, list):
        rep["fatal"] = "Firebase returned an array; record keys must be non-numeric like P110001."
        return rep
    if not isinstance(node, dict):
        rep["fatal"] = "Unexpected response shape."
        return rep

    rep["total"] = len(node)
    seen = set()
    for key, rec in node.items():
        def issue(reason, status="REJECTED", pid=None):
            rep["issues"].append({"key": key, "patient_id": pid, "status": status, "reason": reason})

        if not isinstance(rec, dict):
            issue("record is not an object")
            continue
        pid = rec.get("patient_id")
        if not isinstance(pid, int) or isinstance(pid, bool):
            issue("patient_id missing or not an integer")
            continue
        if key != f"P{pid}":
            issue("record key does not match patient_id", pid=pid)
            continue

        source = expected
        if mode == "shared":
            source = rec.get("source_hospital")
            if source not in HOSPITAL_ID_RANGES:
                issue("source_hospital missing or unknown", pid=pid); continue
            if rec.get("shared_with") != expected:
                issue("shared_with does not match this hospital", pid=pid); continue
            if source == expected:
                issue("record shared with its own source hospital", pid=pid); continue
            if rec.get("consent") is not True:
                issue("no recorded consent for sharing", pid=pid); continue

        if not isinstance(rec.get("age"), int) or isinstance(rec.get("age"), bool):
            issue("age missing or outside 0-120", pid=pid); continue
        if not isinstance(rec.get("name"), str) or not isinstance(rec.get("gender"), str):
            issue("name or gender missing", pid=pid); continue

        clean, reason = validate_patient(rec, source)
        if reason:
            issue(reason, pid=pid); continue
        if rec["gender"] != clean["gender"]:  # must already be upper-case M/F/O
            issue("gender must be M, F or O", pid=pid); continue
        if pid in seen:
            issue("duplicate patient_id within this batch", pid=pid); continue
        seen.add(pid)
        if exists_locally(pid):
            issue("patient_id already exists in SmartCare", status="SKIPPED", pid=pid); continue
        clean["source_hospital"] = source
        clean["key"] = key
        rep["accepted"].append(clean)
    return rep


# ---------------------------------------------------------------- operations
def fetch(sess, path):
    return _db("GET", sess, path)


def share(sess, target, patient_id):
    mine = sess.get("hospital")
    if mine not in HOSPITAL_ID_RANGES:
        raise CloudError("This account has no hospital assigned.")
    if target == mine or target not in HOSPITAL_ID_RANGES:
        raise CloudError("Choose a different receiving hospital.")
    lo, hi = HOSPITAL_ID_RANGES[mine]
    if not lo <= patient_id <= hi:
        raise CloudError(f"You can only share patients of {mine} ({lo}-{hi}).")
    key = f"P{patient_id}"
    rec = _db("GET", sess, f"{ROOT}/{mine}/Patients/{key}")
    if rec is None:
        raise CloudError(f"{key} is not in {mine}'s cloud dataset.")
    rep = validate_records({key: rec}, "dataset", mine, lambda _id: False)
    if not rep["accepted"]:
        raise CloudError("Record failed validation: " + (rep["issues"][0]["reason"] if rep["issues"] else rep["fatal"]))
    p = rep["accepted"][0]
    body = {"patient_id": p["patient_id"], "name": p["name"], "age": p["age"], "gender": p["gender"],
            "source_hospital": mine, "shared_with": target, "consent": True,
            "shared_at": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")}
    _db("PUT", sess, f"{ROOT}/Shared_Records/{target}/{key}", body)
    return body


def revoke(sess, target, patient_id):
    try:
        _db("DELETE", sess, f"{ROOT}/Shared_Records/{target}/P{patient_id}")
    except CloudError as e:
        if e.status in (401, 403):
            raise CloudError(f"DENIED: P{patient_id} does not exist in {target}'s shared records, "
                             f"or it was not shared by {sess.get('hospital')}.", e.status)
        raise
