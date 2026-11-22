# SmartCare Phase 3 - environment variables (PowerShell)
# 1. Copy this file to config/set_env.ps1  (that name is in .gitignore)
# 2. Fill in your values
# 3. Run it in the same terminal before starting SmartCare:  . .\config\set_env.ps1

# --- C++ app (Firebase client) ---
$env:SMARTCARE_FIREBASE_DB_URL  = "https://YOUR-PROJECT-default-rtdb.asia-southeast1.firebasedatabase.app"
$env:SMARTCARE_FIREBASE_API_KEY = "YOUR_WEB_API_KEY"          # Project settings > General > Web API key
$env:SMARTCARE_HOSPITAL_EMAIL   = "hospital-b@smartcare.test" # optional; the app asks if unset

# --- PostgreSQL (replace the hard-coded password in phase2.cpp) ---
$env:PGPASSWORD = "YOUR_POSTGRES_PASSWORD"

# --- Python admin tool (team members only) ---
# Keep the key OUTSIDE the repository folder.
$env:FIREBASE_SERVICE_ACCOUNT = "C:\secrets\smartcare-firebase-adminsdk.json"
$env:FIREBASE_DB_URL          = $env:SMARTCARE_FIREBASE_DB_URL
