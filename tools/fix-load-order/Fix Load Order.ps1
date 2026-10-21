# Purple Map Fix - Fix Load Order
#
# Makes the map fix permanent in your load order, so the game loads the right map
# settings by itself. Purple Map Fix writes a report each time the game starts
# (PurpleMapFix-LoadOrder.txt, next to its log) saying which plugins reset the map and
# which plugins to move. This script applies that move to your plugin list.
#
# What it changes: the plugins that reset the map move up to just before the map plugin
# (for example FWMF's patch), keeping their order. A plugin can't load before its own
# masters, so a master they need that loads later moves up with them. Nothing else
# moves. A backup is made first.
#
# Run it with Mod Organizer 2 and the game closed, from any folder (it finds your MO2
# profile itself). Works for Windows PowerShell 5.1+.

param(
    [string]$PluginsTxt,   # use this plugins.txt instead of finding it
    [string]$Report,       # use this report instead of finding it
    [switch]$Yes,          # don't ask for confirmation
    [switch]$DryRun,       # show what would change, change nothing
    [switch]$Force         # edit plugins.txt even when Vortex manages it
)

$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

# Plugin lists can be in UTF-8 or the old Windows encoding, depending on the tool that
# wrote them. Latin-1 maps every byte to one character and back, so files are read and
# written byte for byte, and names with accents are never changed.
# (File.ReadAllText would ignore that and switch to UTF-8 when a file starts with a
# byte-order mark, so files are read as raw bytes.)
$Bytes = [Text.Encoding]::GetEncoding(28591)
$BOM = [string][char]0xEF + [char]0xBB + [char]0xBF   # a UTF-8 byte-order mark, seen as Latin-1
function Read-Text([string]$path) { return $Bytes.GetString([IO.File]::ReadAllBytes($path)) }
function Read-Lines([string]$path) { return ,[string[]]((Read-Text $path) -split "`r?`n") }

function Say($text) { Write-Host $text }
function Warn($text) { Write-Host $text -ForegroundColor Yellow }
function Fail($text) {
    Write-Host ''
    Write-Host $text -ForegroundColor Red
    exit 1
}
function Norm([string]$line) {
    # Plugin name from a plugins.txt / loadorder.txt line: no byte-order mark, no '*',
    # no spaces around, lowercase.
    $s = $line.Trim()
    if ($s.StartsWith($BOM)) { $s = $s.Substring(3).Trim() }
    if ($s.StartsWith('*')) { $s = $s.Substring(1) }
    return $s.Trim().ToLowerInvariant()
}

# ---------------------------------------------------------------- the report

if (-not $Report) {
    $docs = [Environment]::GetFolderPath('MyDocuments')
    $candidates = @(
        (Join-Path $docs 'My Games\Skyrim Special Edition\SKSE\PurpleMapFix-LoadOrder.txt'),
        (Join-Path $docs 'My Games\Skyrim Special Edition GOG\SKSE\PurpleMapFix-LoadOrder.txt')
    ) | Where-Object { Test-Path -LiteralPath $_ }
    if (-not $candidates) {
        Fail "No load order report found. Start the game once with Purple Map Fix installed (getting to the main menu is enough), then run this again."
    }
    $Report = $candidates | Sort-Object { (Get-Item -LiteralPath $_).LastWriteTime } -Descending | Select-Object -First 1
}

$sections = @{}
$current = $null
foreach ($line in (Read-Lines $Report)) {
    if ($line.StartsWith('#')) { continue }
    if ($line -match '^\[(.+)\]$') { $current = $Matches[1]; $sections[$current] = New-Object System.Collections.Generic.List[string]; continue }
    if ($current -and $line.Trim()) { $sections[$current].Add($line.Trim()) }
}
function Section($name) { if ($sections.ContainsKey($name)) { return ,$sections[$name].ToArray() } return ,@() }

$status = (Section 'status') -join ''
$reportTime = (Get-Item -LiteralPath $Report).LastWriteTime
Say "Purple Map Fix - Fix Load Order"
Say "Report: $Report (written $reportTime)"
Say ''

if ($status -eq 'nothing to do') {
    Say 'Nothing to fix: no plugin resets the map settings in the load order the game last used.'
    exit 0
}
if ($status -eq 'not fixable') {
    Warn ('This can''t be fixed by changing the load order: ' + ((Section 'reason') -join ' '))
    Say 'Purple Map Fix keeps fixing it in game every session, so the map still works.'
    exit 0
}
if ($status -ne 'fixable') { Fail "The report looks damaged (status '$status'). Start the game again to rewrite it." }

$mapPlugins = Section 'map plugins'
$resetBy = Section 'reset by'
$move = Section 'move'
$mastersMoved = Section 'masters moved'
$before = (Section 'before') -join ''
$loaded = Section 'loaded'
if (-not $move -or -not $before) { Fail 'The report has no move in it. Start the game again to rewrite it (this script needs the report from Purple Map Fix 1.1.0 or later).' }

Say 'These plugins reset the map settings, because they load after the map plugins:'
$resetBy | ForEach-Object { Say "  - $_" }
Say ''

# ---------------------------------------------------------------- find the plugin list

function Get-Active([string]$path) {
    $names = New-Object System.Collections.Generic.List[string]
    foreach ($l in (Read-Lines $path)) {
        $t = $l.Trim()
        if ($t.StartsWith($BOM)) { $t = $t.Substring(3).Trim() }
        if ($t.StartsWith('*')) { $names.Add((Norm $t)) }
    }
    return ,$names.ToArray()
}

# How well a plugins.txt matches what the game loaded:
#   Order    = share of its active plugins that the game loaded, in the same order
#   Coverage = share of what the game loaded that it has (a profile that's a subset of
#              another, like LoreRim's Default vs Ultra, scores lower here)
$loadedIndex = @{}
for ($i = 0; $i -lt $loaded.Count; $i++) { $loadedIndex[$loaded[$i].ToLowerInvariant()] = $i }
function Get-Match([string]$path) {
    $active = Get-Active $path
    if ($active.Count -eq 0 -or $loaded.Count -eq 0) { return [pscustomobject]@{ Order = 0.0; Coverage = 0.0 } }
    $last = -1; $inOrder = 0; $present = 0
    foreach ($n in $active) {
        if ($loadedIndex.ContainsKey($n)) {
            $present++
            if ($loadedIndex[$n] -gt $last) { $inOrder++; $last = $loadedIndex[$n] }
        }
    }
    return [pscustomobject]@{ Order = [double]$inOrder / $active.Count; Coverage = [double]$present / $loaded.Count }
}

# Folders that might hold Mod Organizer 2 profiles. The script can be run from anywhere
# (a Downloads folder is fine), so it looks in every place an MO2 install can be found:
#   - above the game folder (Wabbajack lists like LoreRim and Nolvus keep the game
#     inside the MO2 folder) and above this script
#   - MO2's global instances (%LOCALAPPDATA%\ModOrganizer\<instance>)
#   - every MO2 registered to handle Nexus downloads (nxmhandler.ini), which is how
#     portable installs elsewhere on the PC are found
function Get-ProfileFolders {
    $roots = New-Object System.Collections.Generic.List[string]
    function Add-Instance([string]$dir) {
        if (-not $dir) { return }
        $base = $dir
        $prof = $null
        $ini = Join-Path $dir 'ModOrganizer.ini'
        if (Test-Path -LiteralPath $ini) {
            foreach ($l in (Read-Lines $ini)) {
                if ($l -match '^\s*base_directory\s*=\s*(.+?)\s*$') { $base = $Matches[1] -replace '/', '\' }
                if ($l -match '^\s*profiles_directory\s*=\s*(.+?)\s*$') { $prof = $Matches[1] -replace '/', '\' }
            }
        }
        if ($prof) { $roots.Add(($prof -replace '%BASE_DIR%', $base)) }
        $roots.Add((Join-Path $base 'profiles'))
        if ($base -ne $dir) { $roots.Add((Join-Path $dir 'profiles')) }
    }

    $starts = @()
    $gameFolder = (Section 'game folder') -join ''
    if ($gameFolder) { $starts += $gameFolder }
    $starts += $ScriptDir
    foreach ($start in $starts) {
        $dir = $start
        for ($n = 0; $n -lt 6 -and $dir; $n++) {
            Add-Instance $dir
            $dir = Split-Path -Parent $dir
        }
    }
    if ($env:LOCALAPPDATA) {
        $instances = Join-Path $env:LOCALAPPDATA 'ModOrganizer'
        if (Test-Path -LiteralPath $instances) {
            Get-ChildItem -LiteralPath $instances -Directory -ErrorAction SilentlyContinue | ForEach-Object { Add-Instance $_.FullName }
            $nxm = Join-Path $instances 'nxmhandler.ini'
            if (Test-Path -LiteralPath $nxm) {
                foreach ($l in (Read-Lines $nxm)) {
                    if ($l -match '^\s*\d+\\executable\s*=\s*"?(.+?)"?\s*$') {
                        $exe = $Matches[1] -replace '\\\\', '\' -replace '/', '\'
                        try { Add-Instance (Split-Path -Parent $exe) } catch { }
                    }
                }
            }
        }
    }
    return ,@($roots | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } | Select-Object -Unique)
}

if (-not $PluginsTxt) {
    $found = New-Object System.Collections.Generic.List[string]
    foreach ($root in (Get-ProfileFolders)) {
        Get-ChildItem -LiteralPath $root -Directory | ForEach-Object {
            $p = Join-Path $_.FullName 'plugins.txt'
            if ((Test-Path -LiteralPath $p) -and -not $found.Contains($p)) { $found.Add($p) }
        }
    }
    # The game's own list (Vortex, manual installs).
    if ($env:LOCALAPPDATA) {
        foreach ($game in @('Skyrim Special Edition', 'Skyrim Special Edition GOG')) {
            $p = Join-Path $env:LOCALAPPDATA "$game\plugins.txt"
            if (Test-Path -LiteralPath $p) { $found.Add($p) }
        }
    }

    $scored = @($found | ForEach-Object { $m = Get-Match $_; [pscustomobject]@{ Path = $_; Order = $m.Order; Coverage = $m.Coverage } } |
        Where-Object { $_.Order -ge 0.98 } | Sort-Object Coverage -Descending)
    if ($scored.Count -eq 0) {
        Warn "Couldn't find a plugin list that matches the load order the game last used."
        Say  "Looked in $($found.Count) place(s). If you use Mod Organizer 2, the file is in <MO2 folder>\profiles\<your profile>\plugins.txt"
        if ($Yes) { Fail 'Run again with -PluginsTxt <path to plugins.txt>.' }
        $PluginsTxt = (Read-Host 'Paste the full path to your plugins.txt (or press Enter to stop)').Trim('" ')
        if (-not $PluginsTxt) { exit 1 }
    } else {
        $PluginsTxt = $scored[0].Path
        if ($scored.Count -gt 1 -and [math]::Abs($scored[1].Coverage - $scored[0].Coverage) -lt 0.0001) {
            Warn 'More than one profile matches equally well:'
            $scored | ForEach-Object { Say "  $($_.Path)" }
            if ($Yes) { Fail 'Run again with -PluginsTxt <path> to pick one.' }
            $PluginsTxt = (Read-Host 'Paste the path of the one to fix').Trim('" ')
        }
    }
}
if (-not (Test-Path -LiteralPath $PluginsTxt)) { Fail "Not found: $PluginsTxt" }
$PluginsTxt = (Resolve-Path -LiteralPath $PluginsTxt).Path
$folder = Split-Path -Parent $PluginsTxt
$loadOrderTxt = Join-Path $folder 'loadorder.txt'
$isGameList = [bool]$env:LOCALAPPDATA -and $PluginsTxt.StartsWith($env:LOCALAPPDATA, [StringComparison]::OrdinalIgnoreCase)
$usesVortex = $isGameList -and [bool]$env:APPDATA -and (Test-Path -LiteralPath (Join-Path $env:APPDATA 'Vortex'))

Say "Plugin list: $PluginsTxt"
if ((Get-Match $PluginsTxt).Order -lt 0.98) {
    Warn 'Note: this plugin list differs from the load order the game last used. Check the result in your mod manager.'
}

# ---------------------------------------------------------------- safety checks

$running = Get-Process -ErrorAction SilentlyContinue | Where-Object {
    $_.ProcessName -in @('ModOrganizer', 'SkyrimSE', 'SkyrimVR', 'skse64_loader')
}
if ($running -and -not $DryRun) {
    Fail ('Close ' + (($running | ForEach-Object { $_.ProcessName } | Sort-Object -Unique) -join ', ') + ' first. Mod Organizer rewrites the plugin list when it closes, which would undo this.')
}

$locked = Join-Path $folder 'lockedorder.txt'
if (Test-Path -LiteralPath $locked) {
    $lockedNames = (Read-Lines $locked) | Where-Object { $_ -and -not $_.StartsWith('#') } | ForEach-Object { (Norm ($_ -split '\|')[0]) }
    $clash = $move | Where-Object { $lockedNames -contains $_.ToLowerInvariant() }
    if ($clash) { Fail ('These plugins are locked in place in Mod Organizer. Unlock them first: ' + ($clash -join ', ')) }
}

# ---------------------------------------------------------------- the change

# Moves the lines for $move that are below the line for $before to directly above it,
# keeping their order. Returns $null if nothing needs to move.
function Get-Moved([string[]]$lines) {
    $moveSet = @{}
    foreach ($m in $move) { $moveSet[$m.ToLowerInvariant()] = $true }
    $anchorName = $before.ToLowerInvariant()

    $anchorAt = -1
    for ($i = 0; $i -lt $lines.Count; $i++) { if ((Norm $lines[$i]) -eq $anchorName) { $anchorAt = $i; break } }
    if ($anchorAt -lt 0) { throw "'$before' isn't in $([IO.Path]::GetFileName($script:currentFile))" }

    $moving = New-Object System.Collections.Generic.List[string]
    $rest = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($i -gt $anchorAt -and $moveSet.ContainsKey((Norm $lines[$i]))) { $moving.Add($lines[$i]) } else { $rest.Add($lines[$i]) }
    }
    if ($moving.Count -eq 0) { return $null }
    $rest.InsertRange($anchorAt, $moving)
    return ,$rest.ToArray()
}

function Read-ListFile([string]$path) {
    $text = Read-Text $path
    $newline = "`n"; if ($text.Contains("`r`n")) { $newline = "`r`n" }
    $trailing = $text.EndsWith("`n")
    $lines = $text -split "`r?`n"
    if ($trailing) { $lines = $lines[0..($lines.Count - 2)] }
    return [pscustomobject]@{ Lines = [string[]]$lines; Newline = $newline; Trailing = $trailing }
}

$files = @($PluginsTxt)
if (Test-Path -LiteralPath $loadOrderTxt) { $files += $loadOrderTxt }

$changes = @()
foreach ($f in $files) {
    $script:currentFile = $f
    $info = Read-ListFile $f
    $new = Get-Moved $info.Lines
    if ($new) { $changes += [pscustomobject]@{ File = $f; Info = $info; New = $new } }
}

if (-not $changes) {
    Say ''
    Say 'Already fixed: every plugin that reset the map already loads before the map plugin.'
    Say 'Start the game once and the report will update.'
    exit 0
}

Say ''
Say "Plan: move these plugins up to just before '$before', in this order:"
foreach ($m in $move) {
    if ($mastersMoved -contains $m) { Say "  - $m   (needed as a master by a plugin above)" } else { Say "  - $m" }
}
Say 'Nothing else changes. Moving a plugin up can change which plugin wins its other conflicts,'
Say 'so check your game after. The backups let you undo it.'

if ($usesVortex -and -not $Force) {
    # Vortex keeps its own plugin order and rewrites plugins.txt when it deploys or sorts,
    # so editing the file would be undone. Rules are Vortex's lasting way to set order.
    Say ''
    Warn 'You use Vortex, which manages the plugin order itself and would undo an edit to plugins.txt.'
    Say  'Make the same fix in Vortex with "Load after" rules:'
    Say  "  1. Open Vortex -> Plugins tab -> click '$before'."
    Say  '  2. In the panel on the right, open Dependencies (Rules).'
    Say  '  3. Add a "Load after" rule for each of these plugins:'
    $resetBy | ForEach-Object { Say "       - $_" }
    Say  '  4. Click Sort Now. Vortex places them above it (with their masters) and keeps it that way.'
    Say ''
    Say  'No files were changed. (To edit plugins.txt directly anyway, run with -Force.)'
    exit 0
}
if ($isGameList) {
    Say ''
    Warn 'This is the game''s own plugin list. If a mod manager or LOOT sorts plugins later, it may undo this.'
}

if ($DryRun) { Say ''; Say 'Dry run: nothing was changed.'; exit 0 }
if (-not $Yes) {
    Say ''
    $answer = Read-Host 'Make this change? A backup is made first. (Y/N)'
    if ($answer -notmatch '^[Yy]') { Say 'Nothing was changed.'; exit 0 }
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
foreach ($c in $changes) {
    $backup = "$($c.File).before-purplemapfix-$stamp"
    Copy-Item -LiteralPath $c.File -Destination $backup
    $text = ($c.New -join $c.Info.Newline)
    if ($c.Info.Trailing) { $text += $c.Info.Newline }
    [IO.File]::WriteAllText($c.File, $text, $Bytes)
    Say "Updated $([IO.Path]::GetFileName($c.File)) (backup: $([IO.Path]::GetFileName($backup)))"
}

Say ''
Say 'Done. Start the game: the Purple Map Fix log should no longer list any plugins resetting the map.'
Say 'If you sort with LOOT later, it may move them back. Purple Map Fix still fixes the map in game either way.'
Say "To undo, replace the files with the backups ending in .before-purplemapfix-$stamp"
exit 0
