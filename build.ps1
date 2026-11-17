$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    cmake -S . -B build -A x64
    if ($LASTEXITCODE -ne 0) { throw 'Falha ao configurar. Instale Visual Studio 2022 e o Windows SDK.' }
    cmake --build build --config Release
    if ($LASTEXITCODE -ne 0) { throw 'Falha na compilação.' }
    ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Os testes de cor falharam.' }
    Write-Host 'App: build\Release\HdrBridge.exe'
} finally { Pop-Location }
