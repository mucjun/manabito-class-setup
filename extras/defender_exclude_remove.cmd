@echo off
rem Remove ManabitoClassSetup.exe from Microsoft Defender exclusions. Asks for confirmation.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\defender_exclusion.ps1" -Remove
