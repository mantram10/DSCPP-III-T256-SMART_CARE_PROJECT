# ============================================================
# SmartCare Phase 3 - C++ timing on the large test database
# Run from the SmartCare_Phase3 folder:
#   powershell -ExecutionPolicy Bypass -File tests\benchmark_cpp.ps1
# Each run starts phase3.exe, feeds menu choices, and measures total time.
# NOTE: tests 4 and 5 process (change) one appointment/emergency in smartcare_test.
# ============================================================
$env:PGDATABASE = "smartcare_test"
if (-not $env:PGPASSWORD) { $env:PGPASSWORD = "admin" }
$exe = Join-Path $PSScriptRoot "..\phase3.exe"
if (-not (Test-Path $exe)) { Write-Host "phase3.exe not found - compile it first."; exit 1 }

function Time-Run([string]$label, [string]$keys, [int]$repeat = 3) {
    $times = @()
    for ($i = 0; $i -lt $repeat; $i++) {
        $t = Measure-Command { $keys | & $exe | Out-Null }
        $times += $t.TotalSeconds
    }
    $avg = ($times | Measure-Object -Average).Average
    return [pscustomobject]@{ Test = $label; AvgSeconds = [math]::Round($avg, 2) }
}

Write-Host "Benchmarking phase3.exe on database smartcare_test (3 runs each)..." -ForegroundColor Cyan
$results = @()
$results += Time-Run "1. Startup: load all data into C++ + exit" "0`n"
$results += Time-Run "2. Search patient 12345 (hash map)"       "3`n12345`n`n0`n"
$results += Time-Run "3. Display all patients (linked list)"     "2`n`n0`n" 1
$results += Time-Run "4. Process next appointment (queue)"       "7`n`n0`n" 1
$results += Time-Run "5. Process most urgent emergency (heap)"   "9`n`n0`n" 1

$startup = $results[0].AvgSeconds
$results | ForEach-Object {
    $_ | Add-Member -NotePropertyName ExtraOverStartup -NotePropertyValue ([math]::Round($_.AvgSeconds - $startup, 2))
}
$results | Format-Table -AutoSize
$results | Export-Csv -NoTypeInformation tests\benchmark_cpp_results.csv
Write-Host "Saved to tests\benchmark_cpp_results.csv"
