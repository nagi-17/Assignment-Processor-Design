@echo off
setlocal

if not exist build\debugger mkdir build\debugger

echo [1/4] Building debugger helper tests...
g++ -std=c++17 -Wall -Wextra -pedantic -O2 -Isrc/debugger -Isrc/headers -Isrc/headers/latches tests/debugger_tests.cpp src/debugger/debugger.cpp -o build\debugger\debugger_tests.exe
if errorlevel 1 goto fail
build\debugger\debugger_tests.exe
if errorlevel 1 goto fail

echo [2/4] Building callback target tests...
g++ -std=c++17 -Wall -Wextra -pedantic -O2 -Isrc/debugger -Isrc/headers -Isrc/headers/latches tests/debugger_callback_target_test.cpp -o build\debugger\debugger_callback_target_test.exe
if errorlevel 1 goto fail
build\debugger\debugger_callback_target_test.exe
if errorlevel 1 goto fail

echo [3/4] Building four-stage adapter test...
g++ -std=c++17 -Wall -Wextra -pedantic -O2 -Isrc/debugger -Isrc/headers -Isrc/headers/latches tests/debugger_four_stage_adapter_test.cpp src/debugger/debugger.cpp -o build\debugger\four_stage_adapter_test.exe
if errorlevel 1 goto fail
build\debugger\four_stage_adapter_test.exe
if errorlevel 1 goto fail

echo [4/4] Building six-stage adapter test...
g++ -std=c++17 -Wall -Wextra -pedantic -O2 -Isrc/debugger -Isrc/headers -Isrc/headers/latches tests/debugger_six_stage_adapter_test.cpp src/debugger/debugger.cpp -o build\debugger\six_stage_adapter_test.exe
if errorlevel 1 goto fail
build\debugger\six_stage_adapter_test.exe
if errorlevel 1 goto fail

echo.
echo All debugger tests passed.
exit /b 0

:fail
echo.
echo A build or test failed. Read the error above before continuing.
exit /b 1
