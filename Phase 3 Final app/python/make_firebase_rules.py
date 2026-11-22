"""
Generates firebase/database.rules.json for every hospital in validation.py

    python python/make_firebase_rules.py
Then paste the file into Firebase Console > Realtime Database > Rules > Publish.

Rules (enforced by Firebase's server, so a modified app cannot bypass them):
  * a hospital account can read ONLY its own dataset: SmartCare/<own>/Patients
  * a hospital can read ONLY records shared with it:   SmartCare/Shared_Records/<own>
  * a hospital can share ONLY its own, unmodified patients, with consent, to ANOTHER hospital
  * only the hospital that shared a record can revoke it
  * nobody can write hospital datasets through the API (only the admin upload tool)
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validation import HOSPITAL_ID_RANGES  # noqa: E402

SRC = "root.child('SmartCare/' + auth.token.hospital + '/Patients/' + $key"
WRITE_RULE = (
    "auth != null && $target !== auth.token.hospital && ("
    "(!newData.exists() && data.child('source_hospital').val() === auth.token.hospital)"
    " || ("
    "newData.child('source_hospital').val() === auth.token.hospital"
    " && newData.child('shared_with').val() === $target"
    " && newData.child('consent').val() === true"
    f" && {SRC}).exists()"
    + "".join(f" && newData.child('{f}').val() === {SRC} + '/{f}').val()"
              for f in ("patient_id", "name", "age", "gender"))
    + "))"
)


def build_rules():
    smartcare = {h: {".read": f"auth != null && auth.token.hospital === '{h}'"} for h in HOSPITAL_ID_RANGES}
    smartcare["Shared_Records"] = {
        "$target": {
            ".read": "auth != null && auth.token.hospital === $target",
            "$key": {".write": WRITE_RULE},
        }
    }
    return {"rules": {".read": False, ".write": False, "SmartCare": smartcare}}


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firebase", "database.rules.json")
    with open(out, "w") as f:
        json.dump(build_rules(), f, indent=2)
    print(f"Wrote rules for {len(HOSPITAL_ID_RANGES)} hospitals: {', '.join(HOSPITAL_ID_RANGES)}")
    print("Now publish it: Firebase Console > Realtime Database > Rules > paste > Publish")
