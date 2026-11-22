"""
SmartCare Phase 3 - patient record validation (Python side).

These rules intentionally mirror cloud/cloud_import.h in C++.
Python validates BEFORE upload; C++ validates again AFTER download,
because the cloud copy could be edited or corrupted in between.
"""

# The hospital network. Each hospital owns a disjoint patient-ID range, so an ID
# also proves which hospital a record came from. To add a hospital, add one line
# here AND in cloud/cloud_import.h, then run python/make_firebase_rules.py.
HOSPITAL_ID_RANGES = {
    "Hospital_A": (110001, 119999),
    "Hospital_B": (120001, 129999),
    "Hospital_C": (130001, 139999),
    "Hospital_D": (140001, 149999),
    "Hospital_E": (150001, 159999),
}


def dataset_filename(hospital):
    """Hospital_C -> hospital_c_patients.csv"""
    return f"hospital_{hospital.split('_')[-1].lower()}_patients.csv"


def default_email(hospital):
    """Hospital_C -> hospital-c@smartcare.test"""
    return f"hospital-{hospital.split('_')[-1].lower()}@smartcare.test"

REQUIRED_COLUMNS = ("patient_id", "name", "age", "gender")
ALLOWED_GENDERS = {"M", "F", "O"}


def _to_int(value):
    try:
        text = str(value).strip()
        if text == "" or not text.lstrip("-").isdigit():
            return None
        return int(text)
    except (TypeError, ValueError):
        return None


def validate_patient(row, hospital):
    """Return (clean_record, None) if valid, else (None, reason)."""
    if hospital not in HOSPITAL_ID_RANGES:
        return None, f"unknown hospital {hospital!r}"
    for col in REQUIRED_COLUMNS:
        if col not in row:
            return None, f"missing column {col!r}"

    pid = _to_int(row["patient_id"])
    if pid is None:
        return None, "patient_id is not an integer"
    lo, hi = HOSPITAL_ID_RANGES[hospital]
    if not lo <= pid <= hi:
        return None, f"patient_id {pid} outside {hospital} range {lo}-{hi}"

    name = str(row["name"]).strip()
    if not name:
        return None, "name is empty"
    if len(name) > 100:
        return None, "name longer than 100 characters"
    if any(ord(c) < 32 or ord(c) == 127 for c in name) or "\\" in name or ";" in name:
        return None, "name contains disallowed characters"

    age = _to_int(row["age"])
    if age is None or not 0 <= age <= 120:
        return None, "age missing or outside 0-120"

    gender = str(row["gender"]).strip().upper()
    if gender not in ALLOWED_GENDERS:
        return None, "gender must be M, F or O"

    return {"patient_id": pid, "name": name, "age": age, "gender": gender}, None


def validate_dataset(rows, hospital):
    """Validate a list of dict rows. Returns (valid_records, errors).

    errors is a list of (row_number, reason); row_number is 1-based data row.
    Duplicate IDs: the first occurrence is kept, later ones are errors.
    """
    valid, errors, seen = [], [], set()
    for i, row in enumerate(rows, start=1):
        rec, reason = validate_patient(row, hospital)
        if reason:
            errors.append((i, reason))
            continue
        if rec["patient_id"] in seen:
            errors.append((i, f"duplicate patient_id {rec['patient_id']}"))
            continue
        seen.add(rec["patient_id"])
        valid.append(rec)
    return valid, errors
