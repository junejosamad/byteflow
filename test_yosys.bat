@echo off
set PATH=C:\oss-cad-suite\bin;C:\oss-cad-suite\lib;%PATH%
echo Checking Yosys...
C:\oss-cad-suite\bin\yosys.exe --version
if %errorlevel% neq 0 (
    echo Yosys failed with code %errorlevel%
) else (
    echo Yosys success
)
