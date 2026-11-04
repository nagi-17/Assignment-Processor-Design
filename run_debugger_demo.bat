@echo off
setlocal

if not exist build\debugger mkdir build\debugger

echo Building the demonstration CLI. This uses a mock CPU, not the real RISC201 processor.
g++ -std=c++17 -Wall -Wextra -pedantic -O2 -Isrc/debugger -Isrc/headers -Isrc/headers/latches examples/debugger_mock.cpp src/debugger/debugger.cpp src/debugger/debugger_cli.cpp -o build\debugger\debugger_demo.exe
if errorlevel 1 exit /b 1

build\debugger\debugger_demo.exe
