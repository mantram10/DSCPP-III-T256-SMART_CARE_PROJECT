
$ErrorActionPreference = "Stop"

$Repo = "C:\Users\Mantram\OneDrive - Graphic Era University\Documents\PBL 3RD SEM\DSCPP-III-T256-SMART_CARE_PROJECT"
$PgDump = "C:\Program Files\PostgreSQL\18\bin\pg_dump.exe"
$Snapshot = Join-Path $Repo "database\smartcare_snapshot.sql"

Set-Location $Repo

New-Item -ItemType Directory -Force -Path (Join-Path $Repo "database") | Out-Null

& $PgDump -h localhost -p 5432 -U postgres -d smartcare --no-owner --no-privileges -f $Snapshot

if ($LASTEXITCODE -ne 0) {
    throw "Database export failed. GitHub was not updated."
}

git add -- database/smartcare_snapshot.sql

git diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    Write-Output "No database changes to upload."
    exit 0
}

git commit -m "Update SmartCare database snapshot"

if ($LASTEXITCODE -ne 0) {
    throw "Git commit failed."
}

git push

if ($LASTEXITCODE -ne 0) {
    throw "GitHub push failed."
}

Write-Output "SmartCare database snapshot uploaded successfully."
