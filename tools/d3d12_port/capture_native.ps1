# Captures the regression cases of the D3D12 port on Windows (docs/specs/d3d12_port.md): screens S1-S10 as PNGs and effects V1-V12 as
# JSON, from the Debug build's sandbox.exe, in the layout of the web harness's (capture.mjs), so that compare can set them against the
# SwiftShader baseline. The effects cases are tools/d3d12_port/cases.json, which capture.mjs reads too.
#   powershell -File tools\d3d12_port\capture_native.ps1 -Build build-win -Out build-win\capture [-Cases S1,V3] [-List]
# Each case group is one run of the sandbox with a script (sandbox/debug_script.c), on empty storage (NV_STORAGE_DIR): the web
# harness opens a fresh page for each. The viewport of a screen is cut out of the canvas as the web harness clips it.
param(
    [string]$Build = "build-win",
    [string]$Out = "build-win\capture",
    [string[]]$Cases = @(),
    [switch]$List
)

$ErrorActionPreference = "Stop"
$Cases = @($Cases | ForEach-Object { $_ -split "," } | Where-Object { $_ })  # -File gives "S1,V3" as one string
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$exe = Join-Path (Resolve-Path (Join-Path $root $Build)).Path "sandbox\sandbox.exe"
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$scripts = Join-Path $Out "scripts"
New-Item -ItemType Directory -Force $scripts | Out-Null
$vfx = Get-Content (Join-Path $PSScriptRoot "cases.json") -Raw | ConvertFrom-Json

$scenes = @("showcase", "stress", "chess")
$settings = [ordered]@{
    S1 = @{ label = "default";     apply = @() }
    S2 = @{ label = "msaa1";       apply = @("call set_msaa 1") }
    S3 = @{ label = "clamp";       apply = @("call set_post 0 1.0 1 0.04") }
    S4 = @{ label = "aces";        apply = @("call set_post 2 1.0 1 0.04") }
    S5 = @{ label = "bloom-off";   apply = @("call set_post 1 1.0 0 0.04") }
    S6 = @{ label = "shadows-off"; apply = @("call set_shadows 0 0") }
}
$fits = [ordered]@{ whole = 0; viewport = 1; stretch = 2 }
$allIds = @($settings.Keys) + @("S7", "S8", "S9", "S10") + @($vfx.PSObject.Properties.Name)
if ($List) { $allIds -join " "; return }
function Wanted($id) { return ($Cases.Count -eq 0) -or ($Cases -contains $id) }

$defaults = @("call set_msaa 4", "call set_post 1 1.0 1 0.04", "call set_shadows 2048 0", "call set_resolution 0 1 0")
# A still screen: the fixed game clock for a few frames, the clips restarted, then more frames with the clock stopped for what is drawn late.
$still = @("call fixed_dt 0.016666667", "call restart_clips", "call run_frames 6", "frames 30")

$problems = New-Object System.Collections.Generic.List[string]
$run = 0

# Runs the sandbox with `$lines` as its script. The log's errors and warnings are the run's problems.
function Invoke-Sandbox($name, [string[]]$lines) {
    $script:run++
    $file = Join-Path $scripts "$name.script"
    [IO.File]::WriteAllText($file, (($lines + "quit") -join "`n") + "`n", (New-Object Text.UTF8Encoding($false)))
    $store = Join-Path $scripts "storage-$name"
    New-Item -ItemType Directory -Force $store | Out-Null
    $env:NV_STORAGE_DIR = $store
    $log = Join-Path $scripts "$name.log"
    $process = Start-Process -FilePath $exe -ArgumentList "--script", $file, "--out", $Out -WorkingDirectory (Split-Path $exe) `
        -RedirectStandardError $log -PassThru
    $null = $process.Handle  # without this the exit code is not kept
    if (-not $process.WaitForExit(1800000)) { $process.Kill(); $problems.Add("${name}: timed out") }
    $process.WaitForExit()
    $env:NV_STORAGE_DIR = $null
    Get-Content $log | ForEach-Object {
        if ($_ -match "^\[d3d12\]" -and $_ -notmatch "GPU-Based Validation is enabled") { $problems.Add("${name}: " + $_.Substring(0, [Math]::Min(300, $_.Length))) }
    }
    if ($process.ExitCode -ne 0) { $problems.Add("${name}: exit code $($process.ExitCode)") }
    Write-Host "  $name done (exit $($process.ExitCode))"
}

function Show-Scene($index) { return @("call show_scene $index", "frames 25") }

# ---- Screens ---------------------------------------------------------------------------------

if (@($settings.Keys) | Where-Object { Wanted $_ }) {
    $lines = @("size 1280 720", "frames 30")
    for ($scene = 0; $scene -lt $scenes.Count; ++$scene) {
        $lines += Show-Scene $scene
        foreach ($id in $settings.Keys) {
            if (-not (Wanted $id)) { continue }
            $lines += $defaults + $settings[$id].apply + $still + "screenshot $id-$($scenes[$scene])-$($settings[$id].label).png viewport" + "frames 3"
        }
    }
    Invoke-Sandbox "scenes" $lines
}

if (Wanted "S7") {
    Invoke-Sandbox "S7" (@("size 1280 720", "frames 30") + $defaults + "call set_resolution 0 2 0" + $still + "screenshot S7-showcase-scale2.png viewport" + "frames 3")
}

if (Wanted "S8") {
    $lines = @("size 1280 720", "frames 30") + $defaults
    foreach ($fit in $fits.Keys) {
        $lines += @("call set_resolution 1 320 180", "call set_fit $($fits[$fit])") + $still + "screenshot S8-showcase-fixed320x180-$fit.png viewport" + "frames 3"
    }
    Invoke-Sandbox "S8" $lines
}

if (Wanted "S9") {
    # The Textures tab, a row of its list and the picture of that row. A narrow dock shows the list or the picked texture, so each
    # pick starts from a fresh run. The rows and the picture are where the layout puts them at 1280x720 (probed with the web
    # harness's exports: textures_row and textures_rect).
    $picks = @(
        @{ name = "color"; msaa = 4; row = 156; image = "948 216 258 258" },
        @{ name = "depth"; msaa = 1; row = 450; image = "948 192 324 224" },
        @{ name = "depth-multisampled"; msaa = 4; row = 450; image = "948 192 324 224" }
    )
    foreach ($pick in $picks) {
        $lines = @("size 1280 720", "frames 30") + $defaults + @("move 1077 47", "frames 3", "mouse down", "frames 8", "mouse up", "frames 20",
            "call set_msaa $($pick.msaa)") + $still + @("move 1103 $($pick.row)", "frames 3", "mouse down", "frames 8", "mouse up", "frames 10") + $still +
            @("frames 50", "screenshot S9-textures-$($pick.name).png $($pick.image)", "frames 3")
        Invoke-Sandbox "S9-$($pick.name)" $lines
    }
}

if (Wanted "S10") {
    # The desktop UI and a menu. (The phone UI is the web build's: a native build is always the desktop UI.)
    $lines = @("size 1280 720", "frames 30") + $still + @("screenshot S10-desktop.png", "move 139 15", "frames 3", "mouse down", "frames 8", "mouse up", "frames 40",
        "screenshot S10-desktop-menu.png", "frames 3")
    Invoke-Sandbox "S10" $lines
}

# ---- Effects ---------------------------------------------------------------------------------

foreach ($case in $vfx.PSObject.Properties) {
    $id = $case.Name
    $spec = $case.Value
    if (-not (Wanted $id)) { continue }
    $lines = @("size 1280 720", "frames 30")
    $scene = if ($spec.PSObject.Properties["scene"]) { $spec.scene } else { 0 }
    $lines += Show-Scene $scene
    $lines += $defaults
    if ($spec.PSObject.Properties["play"]) {
        # The Play button, at the top center: the sandbox starts Play, and the stress scene's behaviors run.
        $lines += @("move 640 15", "frames 3", "mouse down", "frames 8", "mouse up", "frames 20")
    }
    if ($spec.PSObject.Properties["fast"]) { $lines += @("call set_resolution 0 4 0", "call set_msaa 1") }
    $lines += @("frames 30", "call fixed_dt $($spec.dt.ToString('R', [Globalization.CultureInfo]::InvariantCulture))", "call restart_clips")
    if ($spec.PSObject.Properties["play"]) { $lines += "call set_effects $($spec.play[0]) 0 0 0 0" }

    # What happens at each frame: fires, then the picture, then the dump.
    $events = @{}
    function Add-Event($frame, $kind, $value) {
        if (-not $events.ContainsKey($frame)) { $events[$frame] = @{ fire = @(); shot = $false; dump = $false } }
        if ($kind -eq "fire") { $events[$frame].fire += , $value } else { $events[$frame][$kind] = $true }
    }
    Add-Event 0 "fire" $null; $events[0].fire = @()
    foreach ($fire in @($spec.fire)) { if ($null -ne $fire) { $events[0].fire += , $fire } }
    if ($spec.PSObject.Properties["fireAt"]) {
        foreach ($entry in $spec.fireAt.PSObject.Properties) {
            Add-Event ([int]$entry.Name) "fire" $null
            foreach ($fire in @($entry.Value)) { $events[[int]$entry.Name].fire += , $fire }
        }
    }
    foreach ($frame in @($spec.dump)) { Add-Event ([int]$frame) "dump" $true }
    if ($spec.PSObject.Properties["shots"]) { foreach ($frame in @($spec.shots)) { Add-Event ([int]$frame) "shot" $true } }
    if ($spec.PSObject.Properties["afterwards"]) {
        Add-Event ([int]$spec.afterwards.at) "fire" $null
        foreach ($fire in @($spec.afterwards.fire)) { $events[[int]$spec.afterwards.at].fire += , $fire }
        foreach ($frame in @($spec.afterwards.dump)) { Add-Event ([int]$frame) "dump" $true }
    }
    $counters = if ($spec.PSObject.Properties["counters"] -and $spec.counters) { 1 } else { 0 }
    $lines += "case $id $($spec.dt.ToString('R', [Globalization.CultureInfo]::InvariantCulture)) $counters"
    foreach ($frame in ($events.Keys | Sort-Object { [int]$_ })) {
        $event = $events[$frame]
        $lines += @("call run_frames $frame", "wait-fixed $frame")
        if ($event.fire.Count -gt 0) {
            foreach ($fire in $event.fire) { $lines += "call vfx_fire_n $($fire[0]) $($fire[1])" }
            $lines += "frames 4"
        }
        if ($event.shot) { $lines += @("frames 30", ("screenshot {0}-f{1:D3}.png viewport" -f $id, [int]$frame), "frames 3") }
        if ($event.dump) { $lines += @("frames 6", "case-frame $frame $(if ($counters) { 'counters' } else { 'full' })") }
    }
    $lines += "case-end"
    Invoke-Sandbox $id $lines
}

Write-Host "done: $run run(s), $($problems.Count) problem line(s)"
foreach ($line in ($problems | Select-Object -Unique)) { Write-Host "  problem: $line" }
if ($problems.Count -gt 0) { exit 1 }
