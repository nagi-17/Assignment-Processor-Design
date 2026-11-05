@echo off
setlocal

if not exist build\disassembler mkdir build\disassembler

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Isrc\headers -Isrc\disassembler src\disassembler\main_disassembler.cpp src\disassembler\disassembler.cpp -o build\disassembler\disassembler.exe
if errorlevel 1 (
    echo Disassembler build failed.
    exit /b 1
)

echo Disassembler built: build\disassembler\disassembler.exe
