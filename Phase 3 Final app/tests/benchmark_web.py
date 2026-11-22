"""
SmartCare Phase 3 - website timing on the large test database.

1. Start the website on the test database:  web\\run_web_test.bat
2. In another window:                       python tests/benchmark_web.py
Prints the median response time of each page (5 runs) and saves a CSV.
"""
import csv
import statistics
import sys
import time
import urllib.request

BASE = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:5000"
PAGES = [
    ("Dashboard", "/"),
    ("Patients page 1", "/patients"),
    ("Patients page 500 (deep paging)", "/patients?page=500"),
    ("Search patient by ID", "/patients?q=12345"),
    ("Search patient by name", "/patients?q=rawat"),
    ("Patient detail (with history)", "/patients/12345"),
    ("Appointments queue", "/appointments"),
    ("Emergency board", "/emergency"),
    ("Medical records page 1", "/records"),
    ("Records of one patient", "/records?patient_id=12345"),
    ("Doctors", "/doctors"),
]

rows = []
print(f"Timing {BASE} (median of 5 runs)\n")
print(f"{'Page':40} {'Median ms':>10} {'Size KB':>9}")
for label, path in PAGES:
    times, size = [], 0
    for _ in range(5):
        t0 = time.perf_counter()
        try:
            with urllib.request.urlopen(BASE + path, timeout=60) as r:
                size = len(r.read())
                status = r.status
        except Exception as e:  # noqa: BLE001 - report any failure in the table
            status, size = f"ERROR {e}", 0
        times.append((time.perf_counter() - t0) * 1000)
    med = round(statistics.median(times), 1)
    rows.append({"page": label, "path": path, "median_ms": med, "size_kb": round(size / 1024, 1), "status": status})
    print(f"{label:40} {med:>10} {size / 1024:>9.1f}   {status if status != 200 else ''}")

with open("tests/benchmark_web_results.csv", "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=rows[0].keys())
    w.writeheader()
    w.writerows(rows)
print("\nSaved to tests/benchmark_web_results.csv")
