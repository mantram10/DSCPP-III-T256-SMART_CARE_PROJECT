"""
SmartCare Phase 3 - synthetic Hospital A / Hospital B patient datasets.

Usage (from the repository root):
    python python/generate_hospital_datasets.py
    python python/generate_hospital_datasets.py --count 100 --seed 7
    python python/generate_hospital_datasets.py --with-faulty
    python python/generate_hospital_datasets.py --hospitals Hospital_A,Hospital_C

Outputs (all synthetic - no real people), one file per hospital:
    dataset/hospital_a_patients.csv ... dataset/hospital_e_patients.csv
    dataset/hospital_b_patients_faulty.csv   (only with --with-faulty)

The faulty file contains deliberately broken rows. Upload it with
`--skip-validation` ONLY to demonstrate that the C++ validator rejects
bad cloud data (test case TC-C5 in the guide).
"""
import argparse
import csv
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validation import HOSPITAL_ID_RANGES, dataset_filename, validate_dataset  # noqa: E402

FIRST_M = ["Aarav", "Vivaan", "Aditya", "Arjun", "Rohan", "Kabir", "Ishaan", "Rahul",
           "Vikram", "Siddharth", "Karan", "Manish", "Deepak", "Ankit", "Harsh", "Yash"]
FIRST_F = ["Ananya", "Diya", "Priya", "Sneha", "Kavya", "Isha", "Meera", "Pooja",
           "Riya", "Neha", "Aditi", "Shreya", "Tanvi", "Nidhi", "Simran", "Divya"]
FIRST_O = ["Alex", "Sam", "Noor", "Jai"]
LAST = ["Sharma", "Verma", "Negi", "Rawat", "Bisht", "Joshi", "Gupta", "Singh",
        "Thapa", "Panwar", "Chauhan", "Kumar", "Mehta", "Nautiyal", "Bhatt", "Pant"]

COLUMNS = ["patient_id", "name", "age", "gender"]


def make_patients(hospital, count, rng):
    lo, hi = HOSPITAL_ID_RANGES[hospital]
    if count > hi - lo + 1:
        raise SystemExit(f"count {count} exceeds the ID range for {hospital}")
    rows = []
    for i in range(count):
        g = rng.choices(["M", "F", "O"], weights=[49, 49, 2])[0]
        first = rng.choice({"M": FIRST_M, "F": FIRST_F, "O": FIRST_O}[g])
        # Rough age mix: children, adults, elderly
        age = rng.choice([rng.randint(0, 17), rng.randint(18, 64), rng.randint(65, 95)])
        rows.append({"patient_id": lo + i, "name": f"{first} {rng.choice(LAST)}",
                     "age": age, "gender": g})
    return rows


def make_faulty(base_rows):
    """Valid rows plus one example of every error the validators must catch."""
    rows = [dict(r) for r in base_rows[:5]]
    rows += [
        {"patient_id": 120901, "name": "", "age": 30, "gender": "F"},                  # empty name
        {"patient_id": 120902, "name": "Test Age", "age": -4, "gender": "M"},          # bad age
        {"patient_id": 120903, "name": "Test Age", "age": 150, "gender": "M"},         # bad age
        {"patient_id": 120904, "name": "Test Gender", "age": 40, "gender": "X"},       # bad gender
        {"patient_id": 110050, "name": "Wrong Range", "age": 40, "gender": "F"},       # A's range
        {"patient_id": "abc", "name": "Bad Id", "age": 40, "gender": "F"},             # non-int
        {"patient_id": base_rows[0]["patient_id"], "name": "Dup Id", "age": 40, "gender": "M"},
        {"patient_id": 120905, "name": "Bad;Name", "age": 40, "gender": "M"},          # chars
    ]
    return rows


def write_csv(path, rows):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=COLUMNS)
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--count", type=int, default=50, help="patients per hospital (default 50)")
    ap.add_argument("--seed", type=int, default=42, help="random seed for reproducibility")
    ap.add_argument("--out", default="dataset", help="output folder (default dataset/)")
    ap.add_argument("--hospitals", default="all",
                    help="comma list, e.g. Hospital_A,Hospital_C (default: all " +
                         str(len(HOSPITAL_ID_RANGES)) + ")")
    ap.add_argument("--with-faulty", action="store_true", help="also write a faulty Hospital_B test file")
    args = ap.parse_args()

    chosen = list(HOSPITAL_ID_RANGES) if args.hospitals == "all" else [h.strip() for h in args.hospitals.split(",")]
    for h in chosen:
        if h not in HOSPITAL_ID_RANGES:
            raise SystemExit(f"Unknown hospital {h!r}. Known: {', '.join(HOSPITAL_ID_RANGES)}")

    rng = random.Random(args.seed)
    for hospital in chosen:
        rows = make_patients(hospital, args.count, rng)
        valid, errors = validate_dataset(rows, hospital)
        if errors:  # generator bug guard - should never happen
            raise SystemExit(f"Generated invalid data for {hospital}: {errors[:3]}")
        path = os.path.join(args.out, dataset_filename(hospital))
        write_csv(path, rows)
        print(f"{hospital}: {len(valid)} valid rows -> {path}")

        if hospital == "Hospital_B" and args.with_faulty:
            faulty = make_faulty(rows)
            fpath = os.path.join(args.out, "hospital_b_patients_faulty.csv")
            write_csv(fpath, faulty)
            _, ferr = validate_dataset([{k: str(v) for k, v in r.items()} for r in faulty], hospital)
            print(f"Faulty test file: {len(faulty)} rows, {len(ferr)} expected rejections -> {fpath}")


if __name__ == "__main__":
    main()
