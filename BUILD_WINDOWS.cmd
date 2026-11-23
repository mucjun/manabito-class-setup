@echo off
rem ManabitoClassSetup build script
rem Run from "x64 Native Tools Command Prompt for VS" (not PowerShell).
setlocal
cd /d "%~dp0"
where cl >nul 2>nul || (echo [ERROR] cl.exe not found. Use "x64 Native Tools Command Prompt for VS". & exit /b 1)
if not exist build\obj mkdir build\obj
if not exist build\Release mkdir build\Release

rc /nologo /i res /fo build\obj\app.res res\app.rc || goto :fail
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /W4 /DUNICODE /D_UNICODE /DNOMINMAX /Fo:build\obj\ /Fe:build\Release\ManabitoClassSetup.exe src\main.cpp src\share.cpp build\obj\app.res /link /SUBSYSTEM:WINDOWS /MANIFEST:NO comctl32.lib gdiplus.lib uxtheme.lib shell32.lib ole32.lib oleaut32.lib gdi32.lib user32.lib advapi32.lib version.lib ws2_32.lib iphlpapi.lib bcrypt.lib crypt32.lib Bthprops.lib || goto :fail

robocopy scripts build\Release\scripts /MIR /NJH /NJS /NFL /NDL >nul
robocopy profiles build\Release\profiles /MIR /NJH /NJS /NFL /NDL >nul
copy /y extras\*.cmd build\Release\ >nul

echo.
echo OK: build\Release\ManabitoClassSetup.exe
exit /b 0

:fail
echo.
echo [ERROR] Build failed.
exit /b 1
