# SmartCare Cloud Network Test Report

Run: 2026-11-09 21:36 · Hospitals: 5 · Database: https://smartcare-bbe27-default-rtdb.asia-southeast1.firebasedatabase.app

**Result: 150/150 checks passed — all correct.**

## 1. Dataset read access (each hospital may read only its own)

| actor \ target | A | B | C | D | E |
|---|---|---|---|---|---|
| Hospital_A | ✅ allowed | ⛔ denied | ⛔ denied | ⛔ denied | ⛔ denied |
| Hospital_B | ⛔ denied | ✅ allowed | ⛔ denied | ⛔ denied | ⛔ denied |
| Hospital_C | ⛔ denied | ⛔ denied | ✅ allowed | ⛔ denied | ⛔ denied |
| Hospital_D | ⛔ denied | ⛔ denied | ⛔ denied | ✅ allowed | ⛔ denied |
| Hospital_E | ⛔ denied | ⛔ denied | ⛔ denied | ⛔ denied | ✅ allowed |

## 2. Shared-records inbox access (each hospital may read only its own inbox)

| actor \ target | A | B | C | D | E |
|---|---|---|---|---|---|
| Hospital_A | ✅ allowed | ⛔ denied | ⛔ denied | ⛔ denied | ⛔ denied |
| Hospital_B | ⛔ denied | ✅ allowed | ⛔ denied | ⛔ denied | ⛔ denied |
| Hospital_C | ⛔ denied | ⛔ denied | ✅ allowed | ⛔ denied | ⛔ denied |
| Hospital_D | ⛔ denied | ⛔ denied | ⛔ denied | ✅ allowed | ⛔ denied |
| Hospital_E | ⛔ denied | ⛔ denied | ⛔ denied | ⛔ denied | ✅ allowed |

## 3. Full-mesh consented sharing (diagonal: sharing with self is not attempted)

| actor \ target | A | B | C | D | E |
|---|---|---|---|---|---|
| Hospital_A | – | ✅ allowed | ✅ allowed | ✅ allowed | ✅ allowed |
| Hospital_B | ✅ allowed | – | ✅ allowed | ✅ allowed | ✅ allowed |
| Hospital_C | ✅ allowed | ✅ allowed | – | ✅ allowed | ✅ allowed |
| Hospital_D | ✅ allowed | ✅ allowed | ✅ allowed | – | ✅ allowed |
| Hospital_E | ✅ allowed | ✅ allowed | ✅ allowed | ✅ allowed | – |

## 4. Attack attempts (all must be denied by Firebase rules)

| Attack | Denied |
|---|---|
| share with itself | 5/5 ✅ |
| steal another hospital's patient | 5/5 ✅ |
| tamper patient data | 5/5 ✅ |
| pretend to be another hospital | 5/5 ✅ |
| share without consent | 5/5 ✅ |
| write into another hospital's dataset | 5/5 ✅ |
| revoke a share made by another hospital | 5/5 ✅ |

## 5. Concurrency

| Hospital | Median read (ms) | Errors |
|---|---|---|
| Hospital_A | 203 | 0 |
| Hospital_B | 196 | 0 |
| Hospital_C | 195 | 0 |
| Hospital_D | 190 | 0 |
| Hospital_E | 196 | 0 |

50 requests in 2.3 s (21.5 requests/second).

## 6. Latency

| Operation | Count | Median ms | Max ms |
|---|---|---|---|
| SIGN-IN | 5 | 440 | 509 |
| GET | 115 | 194 | 505 |
| PUT | 50 | 195 | 461 |
| DELETE | 25 | 195 | 337 |
