# Applies a GTA-style keyboard layout to chiaki-ng's keyboard-as-controller mapping.
# Close chiaki-ng before running. Re-map anything in Settings > Keys afterwards.
# Mouse buttons are handled by mouse look (LMB=R2, RMB=L2, MMB=R3, Back=L1, Forward=R1, wheel=D-Pad Up/Down).
#
#   powershell -ExecutionPolicy Bypass -File scripts\windows-gta-keymap.ps1

$path = 'HKCU:\Software\Chiaki\Chiaki\keymap'

$map = [ordered]@{
	'left_stick_up'    = 'W'
	'left_stick_left'  = 'A'
	'left_stick_down'  = 'S'
	'left_stick_right' = 'D'
	'cross'            = 'Shift'  # sprint / confirm
	'box'              = 'Space'  # jump
	'pyramid'          = 'F'      # enter vehicle
	'moon'             = 'R'      # crouch / back
	'l1'               = 'Tab'    # weapon wheel
	'r1'               = 'Q'      # cover
	'l3'               = 'E'      # horn
	'r3'               = 'V'      # look behind
	'touchpad'         = 'M'      # map
	'options'          = 'Esc'    # pause
	'ps'               = 'Home'
	'share'            = 'F2'
}

if (Get-Process -Name chiaki -ErrorAction SilentlyContinue) {
	Write-Error 'chiaki-ng is running; close it first or it will overwrite these settings.'
	exit 1
}

if (-not (Test-Path $path)) {
	New-Item -Path $path -Force | Out-Null
}
foreach ($button in $map.Keys) {
	Set-ItemProperty -Path $path -Name $button -Value $map[$button] -Type String
	'{0,-17} -> {1}' -f $button, $map[$button]
}
'Done. Start chiaki-ng and check Settings > Keys.'
