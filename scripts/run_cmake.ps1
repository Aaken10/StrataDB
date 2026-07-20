$found = Get-ChildItem -Path 'C:\Program Files','C:\Program Files (x86)' -Filter 'cmake.exe' -Recurse -ErrorAction SilentlyContinue -File | Select-Object -First 1
if ($found) {
    $cm = $found.FullName
    Write-Host "Found: $cm"
    & $cm --version
    if (!(Test-Path build)) { New-Item -ItemType Directory -Path build | Out-Null }
    & $cm -S . -B build -DCMAKE_BUILD_TYPE=Release
    & $cm --build build -j --config Release
} else {
    Write-Host 'cmake not found in Program Files'
    exit 2
}
