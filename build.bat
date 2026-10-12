@echo off
setlocal
echo ====================================================
echo   Building Thread Factory (C POSIX pthreads Backend)
echo ====================================================

if exist C:\w64devkit\bin (
    set "PATH=C:\w64devkit\bin;%PATH%"
)

where gcc >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] GCC compiler not found in PATH!
    echo Please ensure MinGW or w64devkit is installed.
    exit /b 1
)

echo Compiling backend source files with GCC...
gcc backend\main.c backend\queue.c backend\worker_pool.c backend\http_server.c -Ibackend -lpthread -lws2_32 -O2 -Wall -Wextra -o factory_server.exe

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] Compilation successful!
    echo Binary generated: factory_server.exe
    echo You can now run "run.bat" or ".\factory_server.exe" to start the simulation.
) else (
    echo.
    echo [ERROR] Compilation failed. Please check the compiler output above.
    exit /b 1
)
endlocal
