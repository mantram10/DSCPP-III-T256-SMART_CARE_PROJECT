"""
LOCAL TEST ONLY - a tiny imitation of the two Firebase REST endpoints
SmartCare uses, so the C++ client can be tested without internet.

It is NOT a substitute for the real Firebase demo. For a closer
local test, prefer the official Firebase Emulator Suite.

    python tests/mock_firebase_server.py --port 8765 --dataset dataset

Then, in another terminal:
    SMARTCARE_FIREBASE_DB_URL=http://127.0.0.1:8765
    SMARTCARE_FIREBASE_AUTH_URL=http://127.0.0.1:8765
    SMARTCARE_FIREBASE_API_KEY=test-key
Accounts: a@smartcare.test / b@smartcare.test, password "test123".
"""
import argparse
import base64
import csv
import json
import os
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

import sys as _sys
_sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "python"))
from validation import HOSPITAL_ID_RANGES, dataset_filename, default_email  # noqa: E402

# Short test emails (a@...) and the real-style ones (hospital-a@...) both work.
USERS = {}
for _h in HOSPITAL_ID_RANGES:
    USERS[_h.split("_")[-1].lower() + "@smartcare.test"] = _h
    USERS[default_email(_h)] = _h
PASSWORD = "test123"
API_KEY = "test-key"
TOKENS = {}  # token -> hospital


def b64(obj):
    return base64.urlsafe_b64encode(json.dumps(obj).encode()).decode().rstrip("=")


def load_csv(path):
    out = {}
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            rec = {k: int(v) if v.lstrip("-").isdigit() else v for k, v in r.items()}
            key = f"P{rec['patient_id']}"
            while key in out:
                key += "_dup"
            out[key] = rec
    return out


def build_store(folder, faulty):
    data = {}
    for h in HOSPITAL_ID_RANGES:
        fname = "hospital_b_patients_faulty.csv" if (faulty and h == "Hospital_B") else dataset_filename(h)
        path = os.path.join(folder, fname)
        if os.path.isfile(path):
            data[h] = {"Patients": load_csv(path)}
    shared_b = {}
    if "Hospital_A" in data:
        a = data["Hospital_A"]["Patients"]
        shared = dict(a["P110005"])
        shared.update(source_hospital="Hospital_A", shared_with="Hospital_B", consent=True)
        no_consent = dict(a["P110006"])
        no_consent.update(source_hospital="Hospital_A", shared_with="Hospital_B", consent=False)
        shared_b = {"P110005": shared, "P110006": no_consent}
    data["Shared_Records"] = {"Hospital_B": shared_b}
    return {"SmartCare": data}


def allowed(hospital, parts):
    # Mirrors firebase/database.rules.json
    if len(parts) >= 2 and parts[0] == "SmartCare" and parts[1] in HOSPITAL_ID_RANGES:
        return hospital == parts[1]
    if len(parts) >= 3 and parts[:2] == ["SmartCare", "Shared_Records"]:
        return hospital == parts[2]
    return False


class Handler(BaseHTTPRequestHandler):
    store = {}

    def send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        u = urlparse(self.path)
        if u.path != "/v1/accounts:signInWithPassword":
            return self.send(404, {"error": {"message": "NOT_FOUND"}})
        if parse_qs(u.query).get("key") != [API_KEY]:
            return self.send(400, {"error": {"message": "API key not valid"}})
        data = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        email = data.get("email")
        if email not in USERS or data.get("password") != PASSWORD:
            return self.send(400, {"error": {"message": "INVALID_LOGIN_CREDENTIALS"}})
        token = ".".join([b64({"alg": "none"}),
                          b64({"email": email, "hospital": USERS[email], "exp": int(time.time()) + 3600}),
                          "sig"])
        TOKENS[token] = USERS[email]
        self.send(200, {"idToken": token, "expiresIn": "3600", "localId": email})

    def do_GET(self):
        u = urlparse(self.path)
        token = parse_qs(u.query).get("auth", [None])[0]
        hospital = TOKENS.get(token)
        if not u.path.endswith(".json"):
            return self.send(404, {"error": "not found"})
        parts = [p for p in u.path[:-5].split("/") if p]
        if not hospital or not allowed(hospital, parts):
            return self.send(401, {"error": "Permission denied"})
        node = self.store
        for p in parts:
            node = node.get(p) if isinstance(node, dict) else None
        self.send(200, node)

    def _auth_parts(self):
        u = urlparse(self.path)
        hospital = TOKENS.get(parse_qs(u.query).get("auth", [None])[0])
        parts = [p for p in u.path[:-5].split("/") if p] if u.path.endswith(".json") else None
        return hospital, parts

    def _node(self, parts):
        node = self.store
        for p in parts:
            node = node.get(p) if isinstance(node, dict) else None
        return node

    def write_allowed(self, hospital, parts, new):
        # Mirrors the ".write" rule in firebase/database.rules.json
        if not hospital or len(parts) != 4 or parts[:2] != ["SmartCare", "Shared_Records"]:
            return False
        target, key = parts[2], parts[3]
        if target == hospital:
            return False
        if new is None:  # delete
            old = self._node(parts)
            return isinstance(old, dict) and old.get("source_hospital") == hospital
        src = self._node(["SmartCare", hospital, "Patients", key])
        return (isinstance(new, dict) and isinstance(src, dict)
                and new.get("source_hospital") == hospital and new.get("shared_with") == target
                and new.get("consent") is True
                and all(new.get(f) == src.get(f) for f in ("patient_id", "name", "age", "gender")))

    def do_PUT(self):
        hospital, parts = self._auth_parts()
        new = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        if parts is None or not self.write_allowed(hospital, parts, new):
            return self.send(401, {"error": "Permission denied"})
        self.store["SmartCare"]["Shared_Records"].setdefault(parts[2], {})[parts[3]] = new
        self.send(200, new)

    def do_DELETE(self):
        hospital, parts = self._auth_parts()
        if parts is None or not self.write_allowed(hospital, parts, None):
            return self.send(401, {"error": "Permission denied"})
        del self.store["SmartCare"]["Shared_Records"][parts[2]][parts[3]]
        self.send(200, None)

    def log_message(self, *a):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--dataset", default="dataset")
    ap.add_argument("--faulty", action="store_true", help="serve the faulty Hospital_B file")
    args = ap.parse_args()
    Handler.store = build_store(args.dataset, args.faulty)
    print(f"Mock Firebase on http://127.0.0.1:{args.port} (local test only)", flush=True)
    HTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
