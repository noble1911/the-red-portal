# Render Farnham's fruit-machine barks with Breeze-TTS-2 inside WSL (~8 GB VRAM, ~5 s per clip).
# Needs the Breeze WSL setup from ../gaming-bot-test (README "your own voice with Breeze").
# Usage: powershell -File tools\voice\render_farnham.ps1 [--only win] [--force]
$script = (wsl -e wslpath -a ($PSScriptRoot + "\render_farnham.py")).Trim()
$wrapper = (wsl -e wslpath -a ((Resolve-Path ($PSScriptRoot + "\..\..\..\gaming-bot-test\voice")).Path)).Trim()
wsl -e bash -lc "~/breeze-tts/.venv/bin/python '$script' --breeze ~/breeze-tts --model ~/breeze-tts-2 --wrapper '$wrapper' $args"
