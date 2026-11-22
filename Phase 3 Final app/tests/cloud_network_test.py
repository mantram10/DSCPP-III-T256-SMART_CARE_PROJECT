"""
SmartCare Phase 3 - CLOUD NETWORK TEST (5 hospitals, real Firebase)

Signs in as every hospital and checks, against the live Firebase security rules:
  1. Read matrix      - each hospital reads ONLY its own dataset           (5 x 5)
  2. Inbox matrix     - each hospital reads ONLY records shared with it    (5 x 5)
  3. Full-mesh share  - every hospital shares with every other (20 pairs); each inbox gets exactly 4
  4. Attacks          - 7 forbidden actions per hospital, all must be DENIED
  5. Revoke           - shares disappear from the receiver's inbox
  6. Concurrency      - all hospitals read at the same time

Before running:  python python/firebase_admin_tool.py setup-network
                 python python/firebase_admin_tool.py upload-all
                 python python/make_firebase_rules.py   (then publish the rules)
Run:             python tests/cloud_network_test.py
Output:          tests/cloud_network_report.md  (tables for your report)

Test shares use patient number 45 of each hospital (e.g. P110045) and are removed at the end.
"""
import argparse
import getpass
import os
import statistics
import sys
import time
from concurrent.futures import ThreadPoolExecutor

ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT_DIR, "web"))
sys.path.insert(0, os.path.join(ROOT_DIR, "python"))


def load_local_env():
    """Use config/local_env.bat values if the variables are not already set."""
    path = os.path.join(ROOT_DIR, "config", "local_env.bat")
    if not os.path.isfile(path):
        return
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.lower().startswith("set ") and "=" in line:
                key, value = line[4:].split("=", 1)
                os.environ.setdefault(key.strip(), value.strip())


load_local_env()
import cloud  # noqa: E402
from validation import HOSPITAL_ID_RANGES, default_email  # noqa: E402

HOSPITALS = list(HOSPITAL_ID_RANGES)
RESULTS = []      # (section, actor, action, target, expected, actual, passed, ms)
LATENCY = {}      # operation -> [ms]


def attempt(method, sess, path, body=None):
    """Returns (allowed, http_status, ms, data). Never raises for 401/403."""
    t0 = time.perf_counter()
    try:
        data = cloud._db(method, sess, path, body)
        ok, status = True, 200
    except cloud.CloudError as e:
        data, ok, status = None, False, e.status
        if e.status == 0:  # network failure is a real error, not a rule decision
            raise
    ms = (time.perf_counter() - t0) * 1000
    LATENCY.setdefault(method, []).append(ms)
    return ok, status, ms, data


def record(section, actor, action, target, expect_allowed, allowed, ms, words=("ALLOW", "DENY")):
    """words: how to print True/False - ("ALLOW","DENY") for access, ("YES","NO") for content checks."""
    passed = expect_allowed == allowed
    RESULTS.append((section, actor, action, target, words[0] if expect_allowed else words[1],
                    words[0] if allowed else words[1], passed, round(ms)))
    return passed


YES_NO = ("YES", "NO")


def short(h):
    return h.split("_")[-1]


def print_matrix(title, matrix):
    print(f"\n{title}")
    print("  actor \\ target " + " ".join(f"{short(t):>5}" for t in HOSPITALS))
    for a in HOSPITALS:
        cells = []
        for t in HOSPITALS:
            exp, act = matrix[(a, t)]
            mark = ("OK" if act else "--") if exp == act else "!!"
            cells.append(f"{mark:>5}")
        print(f"  {a:15}" + " ".join(cells))
    print("  OK = allowed, -- = denied (both as expected), !! = WRONG")


def share_body(rec, source, target, **overrides):
    body = {k: rec[k] for k in ("patient_id", "name", "age", "gender")}
    body.update(source_hospital=source, shared_with=target, consent=True,
                shared_at=time.strftime("%Y-%m-%dT%H:%M:%S+00:00", time.gmtime()))
    body.update(overrides)
    return body


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--password", help="password used in setup-network (asked if omitted)")
    ap.add_argument("--email-pattern", default=None,
                    help="e.g. {x}@smartcare.test; default hospital-{x}@smartcare.test")
    ap.add_argument("--patient", type=int, default=45, help="patient number used for test shares (1-50)")
    ap.add_argument("--report", default=os.path.join(ROOT_DIR, "tests", "cloud_network_report.md"))
    args = ap.parse_args()

    problem = cloud.config_problem()
    if problem:
        sys.exit("Cloud not configured: " + problem + "\nCreate config/local_env.bat first.")
    password = args.password or getpass.getpass("Password of the hospital test accounts (setup-network): ")

    def email_for(h):
        return args.email_pattern.format(x=short(h).lower()) if args.email_pattern else default_email(h)

    # ---------------------------------------------------------------- sign in
    print(f"Signing in {len(HOSPITALS)} hospitals...")
    sess = {}
    for h in HOSPITALS:
        t0 = time.perf_counter()
        try:
            s = cloud.sign_in(email_for(h), password)
        except cloud.CloudError as e:
            sys.exit(f"  {h}: {e}\n  Run: python python/firebase_admin_tool.py setup-network")
        ms = (time.perf_counter() - t0) * 1000
        LATENCY.setdefault("SIGN-IN", []).append(ms)
        ok = s.get("hospital") == h
        record("Sign-in", h, "sign in", h, True, ok, ms, YES_NO)
        print(f"  {h:12} {email_for(h):30} claim={s.get('hospital') or 'MISSING'} {'' if ok else '<-- WRONG'}")
        sess[h] = s
    if any(not r[6] for r in RESULTS):
        sys.exit("Hospital claims are wrong. Run setup-network again, then re-run this test.")

    # ---------------------------------------------------------------- 1. read matrix
    print("\n[1] Read matrix: reading every hospital's dataset as every hospital")
    datasets, read_m = {}, {}
    for a in HOSPITALS:
        for t in HOSPITALS:
            allowed, _, ms, data = attempt("GET", sess[a], f"{cloud.ROOT}/{t}/Patients")
            record("Read dataset", a, "read dataset", t, a == t, allowed, ms)
            read_m[(a, t)] = (a == t, allowed)
            if allowed and a == t:
                datasets[t] = data or {}
    print_matrix("Dataset read access", read_m)
    missing = [h for h in HOSPITALS if not datasets.get(h)]
    if missing:
        sys.exit(f"\nNo uploaded data for {missing}. Run: python python/firebase_admin_tool.py upload-all")

    # ---------------------------------------------------------------- 2. inbox matrix
    print("\n[2] Inbox matrix: reading every hospital's shared-records inbox")
    inbox_m = {}
    for a in HOSPITALS:
        for t in HOSPITALS:
            allowed, _, ms, _ = attempt("GET", sess[a], f"{cloud.ROOT}/Shared_Records/{t}")
            record("Read inbox", a, "read shared inbox", t, a == t, allowed, ms)
            inbox_m[(a, t)] = (a == t, allowed)
    print_matrix("Shared-inbox read access", inbox_m)

    # ---------------------------------------------------------------- 3. full mesh
    print("\n[3] Full-mesh sharing: every hospital shares 1 patient with every other hospital")
    test_key = {h: f"P{HOSPITAL_ID_RANGES[h][0] + args.patient - 1}" for h in HOSPITALS}
    share_m = {}
    for a in HOSPITALS:
        rec = datasets[a].get(test_key[a])
        if rec is None:
            sys.exit(f"{test_key[a]} not in {a}'s dataset; use --patient with a number <= dataset size.")
        for t in HOSPITALS:
            if a == t:
                share_m[(a, t)] = (False, False)
                continue
            allowed, _, ms, _ = attempt("PUT", sess[a], f"{cloud.ROOT}/Shared_Records/{t}/{test_key[a]}",
                                        share_body(rec, a, t))
            record("Share", a, f"share {test_key[a]}", t, True, allowed, ms)
            share_m[(a, t)] = (True, allowed)
    print_matrix("Consented share allowed (diagonal = self, not attempted)", share_m)

    print("\n    Each inbox should now hold exactly one test record from each of the other hospitals:")
    for t in HOSPITALS:
        _, _, ms, inbox = attempt("GET", sess[t], f"{cloud.ROOT}/Shared_Records/{t}")
        inbox = inbox or {}
        got = sorted(inbox[test_key[a]]["source_hospital"] for a in HOSPITALS
                     if a != t and isinstance(inbox.get(test_key[a]), dict))
        rep = cloud.validate_records({k: inbox[k] for k in (test_key[a] for a in HOSPITALS if a != t) if k in inbox},
                                     "shared", t, lambda _id: False)
        ok = len(got) == len(HOSPITALS) - 1 and len(rep["accepted"]) == len(HOSPITALS) - 1
        record("Inbox content", t, "receives 4 valid shares", t, True, ok, ms, YES_NO)
        print(f"    {t:12} from {', '.join(short(g) for g in got) or 'none':15} "
              f"validated {len(rep['accepted'])}/{len(HOSPITALS) - 1}  {'OK' if ok else '<-- WRONG'}")

    # ---------------------------------------------------------------- 4. attacks
    print("\n[4] Attacks: every one of these must be DENIED by Firebase")
    attack_rows = []
    for i, a in enumerate(HOSPITALS):
        other = HOSPITALS[(i + 1) % len(HOSPITALS)]   # a different hospital
        third = HOSPITALS[(i + 2) % len(HOSPITALS)]
        own = datasets[a][test_key[a]]
        attacks = [
            ("share with itself", f"Shared_Records/{a}/{test_key[a]}", share_body(own, a, a), "PUT"),
            ("steal another hospital's patient", f"Shared_Records/{third}/{test_key[other]}",
             share_body(datasets[other][test_key[other]], a, third), "PUT"),
            ("tamper patient data", f"Shared_Records/{other}/{test_key[a]}",
             share_body(own, a, other, name="TAMPERED NAME", age=1), "PUT"),
            ("pretend to be another hospital", f"Shared_Records/{third}/{test_key[other]}",
             share_body(datasets[other][test_key[other]], other, third), "PUT"),
            ("share without consent", f"Shared_Records/{other}/{test_key[a]}",
             share_body(own, a, other, consent=False), "PUT"),
            ("write into another hospital's dataset", f"{other}/Patients/{test_key[a]}", dict(own), "PUT"),
            ("revoke a share made by another hospital", f"Shared_Records/{third}/{test_key[other]}", None, "DELETE"),
        ]
        for label, path, body, method in attacks:
            allowed, status, ms, _ = attempt(method, sess[a], f"{cloud.ROOT}/{path}", body)
            passed = record("Attack", a, label, path.split("/")[1] if "Shared" in path else other, False, allowed, ms)
            attack_rows.append((a, label, passed, status))
    by_label = {}
    for a, label, passed, _ in attack_rows:
        by_label.setdefault(label, []).append(passed)
    for label, ok in by_label.items():
        print(f"    {label:42} denied for {sum(ok)}/{len(ok)} hospitals {'OK' if all(ok) else '<-- SECURITY PROBLEM'}")

    # the tamper/consent attacks targeted real keys: confirm the stored shares were NOT changed
    for i, a in enumerate(HOSPITALS):
        other = HOSPITALS[(i + 1) % len(HOSPITALS)]
        _, _, ms, rec = attempt("GET", sess[other], f"{cloud.ROOT}/Shared_Records/{other}/{test_key[a]}")
        ok = isinstance(rec, dict) and rec.get("name") == datasets[a][test_key[a]]["name"] and rec.get("consent") is True
        record("Attack", a, "stored share unchanged after attacks", other, True, ok, ms, YES_NO)

    # ---------------------------------------------------------------- 5. revoke
    print("\n[5] Revoke: every hospital withdraws its shares")
    for a in HOSPITALS:
        for t in HOSPITALS:
            if a != t:
                allowed, _, ms, _ = attempt("DELETE", sess[a], f"{cloud.ROOT}/Shared_Records/{t}/{test_key[a]}")
                record("Revoke", a, f"revoke {test_key[a]}", t, True, allowed, ms)
    for t in HOSPITALS:
        _, _, ms, inbox = attempt("GET", sess[t], f"{cloud.ROOT}/Shared_Records/{t}")
        left = [test_key[a] for a in HOSPITALS if a != t and test_key[a] in (inbox or {})]
        record("Revoke", t, "inbox empty of test shares", t, True, not left, ms, YES_NO)
        print(f"    {t:12} test shares remaining: {len(left)} {'OK' if not left else '<-- WRONG'}")

    # ---------------------------------------------------------------- 6. concurrency
    print("\n[6] Concurrency: all hospitals read their own dataset 10 times, at the same time")
    def worker(h):
        times, errors = [], 0
        for _ in range(10):
            try:
                allowed, _, ms, data = attempt("GET", sess[h], f"{cloud.ROOT}/{h}/Patients")
                errors += 0 if (allowed and data) else 1
                times.append(ms)
            except cloud.CloudError:
                errors += 1
        return h, times, errors
    t0 = time.perf_counter()
    with ThreadPoolExecutor(max_workers=len(HOSPITALS)) as pool:
        out = list(pool.map(worker, HOSPITALS))
    wall = time.perf_counter() - t0
    total_req = sum(len(t) for _, t, _ in out)
    for h, times, errors in out:
        record("Concurrency", h, "10 parallel reads without error", h, True, errors == 0,
               statistics.median(times) if times else 0, YES_NO)
        print(f"    {h:12} median {statistics.median(times):7.0f} ms   errors {errors}")
    print(f"    {total_req} requests in {wall:.1f} s = {total_req / wall:.1f} requests/second")

    # ---------------------------------------------------------------- summary + report
    passed = sum(1 for r in RESULTS if r[6])
    failed = [r for r in RESULTS if not r[6]]
    print(f"\n{'=' * 60}\nRESULT: {passed}/{len(RESULTS)} checks passed")
    for r in failed[:20]:
        print(f"  FAILED: {r[1]} {r[2]} -> {r[3]}: expected {r[4]}, got {r[5]}")
    print("\nLatency (ms):  operation   count  median   max")
    for op, v in LATENCY.items():
        print(f"               {op:10} {len(v):5} {statistics.median(v):7.0f} {max(v):6.0f}")

    write_report(args.report, read_m, inbox_m, share_m, by_label, out, wall, total_req, passed, failed)
    print(f"\nReport written to {args.report}")
    sys.exit(0 if not failed else 1)


def md_matrix(matrix, legend_self="self"):
    lines = ["| actor \\ target | " + " | ".join(short(t) for t in HOSPITALS) + " |",
             "|---|" + "---|" * len(HOSPITALS)]
    for a in HOSPITALS:
        cells = []
        for t in HOSPITALS:
            exp, act = matrix[(a, t)]
            if (a, t) in matrix and a == t and not exp and not act and legend_self == "-":
                cells.append("–")
            elif exp != act:
                cells.append("**WRONG**")
            else:
                cells.append("✅ allowed" if act else "⛔ denied")
        lines.append(f"| {a} | " + " | ".join(cells) + " |")
    return "\n".join(lines)


def write_report(path, read_m, inbox_m, share_m, by_label, conc, wall, total_req, passed, failed):
    now = time.strftime("%Y-%m-%d %H:%M")
    L = [f"# SmartCare Cloud Network Test Report", "",
         f"Run: {now} · Hospitals: {len(HOSPITALS)} · Database: {os.environ.get('SMARTCARE_FIREBASE_DB_URL', '')}", "",
         f"**Result: {passed}/{len(RESULTS)} checks passed" + (" — all correct.**" if not failed else f" — {len(failed)} FAILED.**"),
         "", "## 1. Dataset read access (each hospital may read only its own)", "", md_matrix(read_m),
         "", "## 2. Shared-records inbox access (each hospital may read only its own inbox)", "", md_matrix(inbox_m),
         "", "## 3. Full-mesh consented sharing (diagonal: sharing with self is not attempted)", "",
         md_matrix(share_m, "-"),
         "", "## 4. Attack attempts (all must be denied by Firebase rules)", "",
         "| Attack | Denied |", "|---|---|"]
    L += [f"| {label} | {sum(v)}/{len(v)} {'✅' if all(v) else '❌'} |" for label, v in by_label.items()]
    L += ["", "## 5. Concurrency", "", "| Hospital | Median read (ms) | Errors |", "|---|---|---|"]
    L += [f"| {h} | {statistics.median(t):.0f} | {e} |" for h, t, e in conc]
    L += ["", f"{total_req} requests in {wall:.1f} s ({total_req / wall:.1f} requests/second).",
          "", "## 6. Latency", "", "| Operation | Count | Median ms | Max ms |", "|---|---|---|---|"]
    L += [f"| {op} | {len(v)} | {statistics.median(v):.0f} | {max(v):.0f} |" for op, v in LATENCY.items()]
    if failed:
        L += ["", "## Failed checks", "", "| Section | Actor | Action | Target | Expected | Got |", "|---|---|---|---|---|---|"]
        L += [f"| {r[0]} | {r[1]} | {r[2]} | {r[3]} | {r[4]} | {r[5]} |" for r in failed]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
