@echo off
rem Add ManabitoClassSetup.exe (Program Files) to Microsoft Defender exclusions. Asks for confirmation.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\defender_exclusion.ps1"
