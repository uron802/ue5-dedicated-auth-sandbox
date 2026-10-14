param([switch]$ValidateOnly)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$projectPath = Join-Path $projectRoot 'GameProject20260916.uproject'
$keyName = 'PLAYFAB_TITLE_SECRET_KEY'
$previousKey = [Environment]::GetEnvironmentVariable($keyName, 'Process')
$secureKey = $null

try {
    if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
        throw 'Project file not found. Keep this launcher in the project folder.'
    }

    $project = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
    $engineRoot = 'C:\UnrealEngine\UE_5.8'
    $engineRegistry = 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\' + $project.EngineAssociation
    $installedEngine = Get-ItemProperty -LiteralPath $engineRegistry -Name InstalledDirectory -ErrorAction SilentlyContinue
    if ($installedEngine -and $installedEngine.InstalledDirectory) {
        $engineRoot = $installedEngine.InstalledDirectory
    }
    $editorPath = Join-Path $engineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    if (-not (Test-Path -LiteralPath $editorPath -PathType Leaf)) {
        throw "Unreal Editor not found: $editorPath"
    }

    if ($ValidateOnly) {
        Write-Host "Project: $projectPath"
        Write-Host "Editor:  $editorPath"
        Write-Host 'Launcher paths are valid. No key requested; no editor launched.'
        return
    }

    if ([System.Diagnostics.Process]::GetProcessesByName('UnrealEditor').Count -gt 0) {
        throw 'Save and close Unreal Editor first, then run this launcher again. An existing editor cannot inherit the new key.'
    }

    $secureKey = Read-Host 'PlayFab Secret Key (hidden)' -AsSecureString
    if ($secureKey.Length -eq 0) {
        throw 'No key entered. Nothing was launched.'
    }

    # The child editor inherits this process environment. Never put the key
    # in command-line arguments, project settings, or a persistent environment.
    [Environment]::SetEnvironmentVariable($keyName, [System.Net.NetworkCredential]::new('', $secureKey).Password, 'Process')
    $editorProcess = Start-Process -FilePath $editorPath -ArgumentList ('"' + $projectPath + '"') -WindowStyle Normal -PassThru
    Write-Host "Unreal Editor started (PID $($editorProcess.Id)). The key was not saved to disk."
}
catch {
    Write-Host ('Error: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
finally {
    [Environment]::SetEnvironmentVariable($keyName, $previousKey, 'Process')
    $previousKey = $null
    if ($null -ne $secureKey) { $secureKey.Dispose(); $secureKey = $null }
}
