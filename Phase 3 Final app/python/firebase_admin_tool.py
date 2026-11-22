"""
SmartCare Phase 3 - Firebase admin tool (run by the project team only).

Requires:  pip install -r python/requirements.txt
Environment (never commit these values):
    FIREBASE_SERVICE_ACCOUNT  path to the service-account JSON key (keep OUTSIDE the repo)
    FIREBASE_DB_URL           e.g. https://<project>-default-rtdb.asia-southeast1.firebasedatabase.app

Commands:
    setup-user --email a@smartcare.test --hospital Hospital_A
        Create (or update) a Firebase Auth user and attach the custom claim
        {"hospital": "Hospital_A"}. The security rules use this claim.

    upload --hospital Hospital_A --csv dataset/hospital_a_patients.csv [--dry-run] [--skip-validation]
        Validate the CSV and write it to SmartCare/<hospital>/Patients (replaces that node).

    share --from Hospital_A --to Hospital_B --patient-id 110005 --consent
        Copy one patient from <from>/Patients into Shared_Records/<to>/ with
        provenance and consent metadata. Refuses without --consent.

    revoke --to Hospital_B --patient-id 110005
        Remove a shared record.

    list --path SmartCare/Shared_Records/Hospital_B
        Print a node (admin view, bypasses rules).

  Whole-network commands (all hospitals in validation.py):
    setup-network
        Create/update hospital-a@ ... hospital-e@smartcare.test with ONE password (asked once).
    upload-all [--folder dataset]
        Validate and upload every hospital's CSV.
    seed-shares --per-pair 2
        Every hospital shares N of its own patients (with consent) with every other hospital.
    clear-shares
        Delete all Shared_Records (start a fresh sharing test).
    status
        Count patients and shared records per hospital.

The Admin SDK talks to Firebase over authenticated HTTPS and bypasses
security rules, which is why this script must never ship inside the
C++ app and the key must never be committed to GitHub.
"""
import argparse
import csv
import datetime as dt
import getpass
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from validation import HOSPITAL_ID_RANGES, dataset_filename, default_email, validate_dataset  # noqa: E402

ROOT = "SmartCare"


def init_firebase():
    key = os.environ.get("FIREBASE_SERVICE_ACCOUNT")
    url = os.environ.get("FIREBASE_DB_URL")
    if not key or not url:
        sys.exit("Set FIREBASE_SERVICE_ACCOUNT and FIREBASE_DB_URL first (see the guide).")
    if not os.path.isfile(key):
        sys.exit(f"Service-account file not found: {key}")
    try:
        import firebase_admin
        from firebase_admin import credentials
    except ImportError:
        sys.exit("firebase-admin is not installed: pip install -r python/requirements.txt")
    if not firebase_admin._apps:
        firebase_admin.initialize_app(credentials.Certificate(key), {"databaseURL": url})


def check_hospital(name):
    if name not in HOSPITAL_ID_RANGES:
        sys.exit(f"Unknown hospital {name!r}. Use one of: {', '.join(HOSPITAL_ID_RANGES)}")


def read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def to_firebase_node(records):
    # Keys MUST be non-numeric ("P110001"); purely numeric keys can make
    # Firebase return the node as a JSON array instead of an object.
    return {f"P{r['patient_id']}": r for r in records}


def raw_node_for_fault_test(rows):
    """Upload rows as-is (types coerced where possible) to test the C++ validator."""
    node, counts = {}, {}
    for r in rows:
        rec = {}
        for k, v in r.items():
            rec[k] = int(v) if str(v).lstrip("-").isdigit() else v
        key = f"P{rec.get('patient_id')}"
        if key in node:  # keep duplicates visible instead of silently overwriting
            counts[key] = counts.get(key, 0) + 1
            key = f"{key}_dup{counts[key]}"
        node[key] = rec
    return node


def cmd_setup_user(args):
    check_hospital(args.hospital)
    init_firebase()
    from firebase_admin import auth

    password = getpass.getpass(f"Password for {args.email} (min 6 chars, not echoed): ")
    if len(password) < 6:
        sys.exit("Password too short.")
    try:
        user = auth.get_user_by_email(args.email)
        auth.update_user(user.uid, password=password)
        print(f"Updated existing user {args.email} (uid {user.uid})")
    except auth.UserNotFoundError:
        user = auth.create_user(email=args.email, password=password)
        print(f"Created user {args.email} (uid {user.uid})")
    auth.set_custom_user_claims(user.uid, {"hospital": args.hospital})
    print(f"Claim set: hospital = {args.hospital}. The user must sign in again to receive it.")


def cmd_upload(args):
    check_hospital(args.hospital)
    rows = read_csv(args.csv)
    if args.skip_validation:
        print("WARNING: --skip-validation uploads unvalidated rows. Use only for validator testing.")
        node = raw_node_for_fault_test(rows)
    else:
        valid, errors = validate_dataset(rows, args.hospital)
        for row_no, reason in errors:
            print(f"  row {row_no}: {reason}")
        if errors:
            sys.exit(f"{len(errors)} invalid row(s); fix the CSV or regenerate it. Nothing uploaded.")
        node = to_firebase_node(valid)

    path = f"{ROOT}/{args.hospital}/Patients"
    print(f"{len(node)} record(s) ready for {path}")
    if args.dry_run:
        print(json.dumps(dict(list(node.items())[:3]), indent=2))
        print("Dry run: nothing uploaded.")
        return

    init_firebase()
    from firebase_admin import db
    db.reference(path).set(node)  # set() replaces the node: re-running is idempotent
    print(f"Uploaded to {path}")


def cmd_share(args):
    check_hospital(args.src)
    check_hospital(args.to)
    if args.src == args.to:
        sys.exit("Source and target hospital must differ.")
    if not args.consent:
        sys.exit("Refusing to share without --consent (records patient consent for this exchange).")
    init_firebase()
    from firebase_admin import db

    key = f"P{args.patient_id}"
    rec = db.reference(f"{ROOT}/{args.src}/Patients/{key}").get()
    if rec is None:
        sys.exit(f"{key} not found in {args.src}/Patients")
    shared = dict(rec)
    shared.update({
        "source_hospital": args.src,
        "shared_with": args.to,
        "consent": True,
        "shared_at": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
    })
    db.reference(f"{ROOT}/Shared_Records/{args.to}/{key}").set(shared)
    print(f"Shared {key} from {args.src} to {args.to}")


def cmd_revoke(args):
    check_hospital(args.to)
    init_firebase()
    from firebase_admin import db
    db.reference(f"{ROOT}/Shared_Records/{args.to}/P{args.patient_id}").delete()
    print(f"Revoked P{args.patient_id} for {args.to}")


def cmd_list(args):
    init_firebase()
    from firebase_admin import db
    print(json.dumps(db.reference(args.path).get(), indent=2))


def _ask_password(prompt):
    pw = getpass.getpass(prompt)
    if len(pw) < 6:
        sys.exit("Password too short (min 6).")
    return pw


def cmd_setup_network(args):
    init_firebase()
    from firebase_admin import auth
    password = _ask_password("One password for ALL hospital test accounts (min 6 chars, not echoed): ")
    for hospital in HOSPITAL_ID_RANGES:
        email = default_email(hospital)
        try:
            user = auth.get_user_by_email(email)
            auth.update_user(user.uid, password=password)
            state = "updated"
        except auth.UserNotFoundError:
            user = auth.create_user(email=email, password=password)
            state = "created"
        auth.set_custom_user_claims(user.uid, {"hospital": hospital})
        print(f"  {hospital:12} {email:30} {state}")
    print("Done. Accounts must sign in again to receive their hospital claim.")


def cmd_upload_all(args):
    init_firebase()
    from firebase_admin import db
    for hospital in HOSPITAL_ID_RANGES:
        path = os.path.join(args.folder, dataset_filename(hospital))
        if not os.path.isfile(path):
            print(f"  {hospital:12} skipped (no {path}; run generate_hospital_datasets.py)")
            continue
        valid, errors = validate_dataset(read_csv(path), hospital)
        if errors:
            print(f"  {hospital:12} NOT uploaded: {len(errors)} invalid row(s), first: row {errors[0][0]} {errors[0][1]}")
            continue
        db.reference(f"{ROOT}/{hospital}/Patients").set(to_firebase_node(valid))
        print(f"  {hospital:12} {len(valid):5} patients uploaded")


def cmd_seed_shares(args):
    """Every hospital shares N of its own patients with every other hospital."""
    init_firebase()
    from firebase_admin import db
    now = dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds")
    total = 0
    for src in HOSPITAL_ID_RANGES:
        patients = db.reference(f"{ROOT}/{src}/Patients").get() or {}
        keys = sorted(patients)
        if not keys:
            print(f"  {src}: no patients uploaded, skipped")
            continue
        targets = [t for t in HOSPITAL_ID_RANGES if t != src]
        for ti, target in enumerate(targets):
            for j in range(args.per_pair):
                key = keys[(ti * args.per_pair + j) % len(keys)]  # different patients per target
                rec = dict(patients[key])
                rec.update(source_hospital=src, shared_with=target, consent=True, shared_at=now)
                db.reference(f"{ROOT}/Shared_Records/{target}/{key}").set(rec)
                total += 1
    print(f"Created {total} consented shares across {len(HOSPITAL_ID_RANGES)} hospitals.")


def cmd_clear_shares(args):
    init_firebase()
    from firebase_admin import db
    db.reference(f"{ROOT}/Shared_Records").delete()
    print("All shared records deleted.")


def cmd_status(args):
    init_firebase()
    from firebase_admin import db
    print(f"  {'Hospital':12} {'Patients':>9} {'Shared with it':>15}  From")
    for h in HOSPITAL_ID_RANGES:
        n = len(db.reference(f"{ROOT}/{h}/Patients").get(shallow=True) or {})
        shared = db.reference(f"{ROOT}/Shared_Records/{h}").get() or {}
        sources = {}
        for rec in shared.values():
            if isinstance(rec, dict):
                sources[rec.get("source_hospital", "?")] = sources.get(rec.get("source_hospital", "?"), 0) + 1
        print(f"  {h:12} {n:9} {len(shared):15}  " + ", ".join(f"{k}:{v}" for k, v in sorted(sources.items())))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("setup-user")
    p.add_argument("--email", required=True)
    p.add_argument("--hospital", required=True)
    p.set_defaults(func=cmd_setup_user)

    p = sub.add_parser("upload")
    p.add_argument("--hospital", required=True)
    p.add_argument("--csv", required=True)
    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--skip-validation", action="store_true")
    p.set_defaults(func=cmd_upload)

    p = sub.add_parser("share")
    p.add_argument("--from", dest="src", required=True)
    p.add_argument("--to", required=True)
    p.add_argument("--patient-id", type=int, required=True)
    p.add_argument("--consent", action="store_true")
    p.set_defaults(func=cmd_share)

    p = sub.add_parser("revoke")
    p.add_argument("--to", required=True)
    p.add_argument("--patient-id", type=int, required=True)
    p.set_defaults(func=cmd_revoke)

    p = sub.add_parser("list")
    p.add_argument("--path", required=True)
    p.set_defaults(func=cmd_list)

    p = sub.add_parser("setup-network")
    p.set_defaults(func=cmd_setup_network)

    p = sub.add_parser("upload-all")
    p.add_argument("--folder", default="dataset")
    p.set_defaults(func=cmd_upload_all)

    p = sub.add_parser("seed-shares")
    p.add_argument("--per-pair", type=int, default=2)
    p.set_defaults(func=cmd_seed_shares)

    p = sub.add_parser("clear-shares")
    p.set_defaults(func=cmd_clear_shares)

    p = sub.add_parser("status")
    p.set_defaults(func=cmd_status)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
