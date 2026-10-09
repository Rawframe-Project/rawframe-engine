# A running client's UI as UI Automation clients read it (Windows, D579):
# the main window of the process named is found, and its tree is read
# through UI Automation until a button the pattern names is in it, or any
# button where none is given, a minute at most (the first asking makes the
# client's access, and the tree comes after it; the title bar's buttons are
# there before it). Prints one line a node, "uia: <control type> <name>".
# Given a pattern, the first button whose name matches it is pressed through
# its Invoke pattern, as a screen reader presses it: "uia: pressed <name>".
#
#   powershell -File tools/uia_read.ps1 <process name> [<name pattern>]
param([Parameter(Mandatory = $true)][string]$Process, [string]$Press = "")
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$automation = [System.Windows.Automation.AutomationElement]
$deadline = (Get-Date).AddSeconds(60)

$nodes = @()
while ((Get-Date) -lt $deadline) {
    $running = Get-Process -Name $Process -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } |
        Select-Object -First 1
    if ($running) {
        try {
            $window = $automation::FromHandle($running.MainWindowHandle)
            $nodes = @($window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                    [System.Windows.Automation.Condition]::TrueCondition))
        } catch {
            $nodes = @()
        }
        if ($nodes | Where-Object {
                $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and
                ($Press -eq "" -or $_.Current.Name -match $Press) }) {
            break
        }
    }
    Start-Sleep -Milliseconds 500
}
if ($nodes.Count -eq 0) {
    Write-Output "uia: no tree of $Process"
    exit 1
}
foreach ($node in $nodes) {
    Write-Output ("uia: " + $node.Current.ControlType.ProgrammaticName.Replace("ControlType.", "") + " " + $node.Current.Name)
}
if ($Press -ne "") {
    $button = $nodes | Where-Object {
        $_.Current.ControlType -eq [System.Windows.Automation.ControlType]::Button -and $_.Current.Name -match $Press
    } | Select-Object -First 1
    if (-not $button) {
        Write-Output "uia: no button matching $Press"
        exit 1
    }
    $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Write-Output ("uia: pressed " + $button.Current.Name)
}
exit 0
