@echo off
setlocal

if "%~2"=="" (
    echo Usage: run_disassembler.bat input.bin output.s [options]
    exit /b 1
)

if not exist build\disassembler\disassembler.exe call build_disassembler.bat
if errorlevel 1 exit /b 1

build\disassembler\disassembler.exe %*
