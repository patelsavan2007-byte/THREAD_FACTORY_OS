@echo off
setlocal
echo ====================================================
echo   Starting Thread Factory Simulation Server
echo ====================================================

if not exist factory_server.exe (
    echo factory_server.exe not found! Running build.bat first...
    call build.bat
    if %ERRORLEVEL% neq 0 exit /b 1
)

echo Opening web browser at http://localhost:8080/ ...
start http://localhost:8080/

echo Starting C backend server on port 8080...
echo Press Ctrl+C in this terminal to stop the server.
echo.
factory_server.exe 8080
endlocal
