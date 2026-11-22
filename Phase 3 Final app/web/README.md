# SmartCare Web (Phase 3)

A local web interface for SmartCare. It uses the **same PostgreSQL database** as `phase3.exe`,
so both always show the same data, and the **same rules**:

| Feature | Behaviour (same as the C++ app) |
|---|---|
| Patients, Doctors, Medical Records | Full create, read, update, delete. Deletes are blocked while related records exist. |
| Appointments | Queue: "Process next" always takes the oldest waiting appointment (FIFO). |
| Emergency | Priority queue: priority 1 first; equal priority → earliest arrival first. Rows use triage colours. |
| Hospital Network | Same graph; BFS uses a queue, DFS a stack. Visiting order matches `phase3.exe`. |
| Cloud Exchange | Firebase sign-in per hospital, validation report before import, consent-based share and revoke. |
| Import Log | Every imported, skipped and rejected cloud record, with the reason. |
| Recent Actions | Stack of actions in this session (newest first). |

## Setup (once)

```powershell
cd C:\Users\Mantram\Downloads\SmartCare_Phase3
python -m pip install -r web/requirements.txt
copy config\local_env.example.bat config\local_env.bat
notepad config\local_env.bat     # fill in database URL and Web API key
```

## Run

Double-click `web\run_web.bat`. The browser opens http://127.0.0.1:5000.
Stop the server with Ctrl+C in its window.

## Notes

- The server listens on 127.0.0.1 only, so it is not reachable from other computers.
- Forms are protected with a per-session token (CSRF) and all output is HTML-escaped.
- If `phase3.exe` is open at the same time, restart it to reload queues changed in the web app.
- Tests: `python tests/test_web.py` (uses an in-memory SQLite test database, not your real data).
