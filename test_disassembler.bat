@echo off
setlocal

if not exist build\disassembler mkdir build\disassembler

g++ -std=c++17 -Wall -Wextra -Wpedantic -O2 -Isrc\headers -Isrc\disassembler tests\disassembler_tests.cpp src\disassembler\disassembler.cpp -o build\disassembler\disassembler_tests.exe
if errorlevel 1 (
    echo Disassembler test build failed.
    exit /b 1
)

build\disassembler\disassembler_tests.exe
if errorlevel 1 exit /b 1

echo All disassembler tests passed.
