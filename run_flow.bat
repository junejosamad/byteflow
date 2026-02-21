@echo off
echo === STEP 1: LOGIC SYNTHESIS (Yosys) ===
yosys -s benchmarks\synth.tcl
if %errorlevel% neq 0 (
    echo.
    echo Yosys failed or not found. Please ensure Yosys is in your PATH.
    echo You can try running this manually from the OSS CAD Suite terminal.
    exit /b 1
)

echo.
echo === STEP 2: PHYSICAL DESIGN (OpenEDA) ===
build\Debug\open_eda.exe benchmarks\counter_structural.v
if %errorlevel% neq 0 exit /b %errorlevel%

echo.
echo === FLOW COMPLETE ===
echo Generated: layout_routed.svg, shift_reg_load.py
