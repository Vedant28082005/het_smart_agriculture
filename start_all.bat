@echo off
rem Starts Mosquitto, the backend and the dashboard, each in its own window.
rem Close a window to stop that service. First run also installs dependencies.
set ROOT=%~dp0
set MOSQ="C:\Program Files\mosquitto\mosquitto.exe"

if not exist %MOSQ% (
  echo Mosquitto not found at %MOSQ%. Install it from https://mosquitto.org/download/
  pause
  exit /b 1
)

if not exist "%ROOT%backend\.venv\Scripts\python.exe" (
  echo Setting up backend...
  python -m venv "%ROOT%backend\.venv" || (echo Python 3.11+ is required & pause & exit /b 1)
  "%ROOT%backend\.venv\Scripts\python.exe" -m pip install -r "%ROOT%backend\requirements.txt"
)
if not exist "%ROOT%backend\.env" copy "%ROOT%backend\.env.example" "%ROOT%backend\.env" >nul

if not exist "%ROOT%dashboard\node_modules" (
  echo Setting up dashboard...
  pushd "%ROOT%dashboard"
  call npm install || (echo Node.js 20.19+ is required & pause & exit /b 1)
  popd
)
if not exist "%ROOT%dashboard\.env" copy "%ROOT%dashboard\.env.example" "%ROOT%dashboard\.env" >nul

start "Mosquitto (MQTT broker)" cmd /k "cd /d %ROOT% && %MOSQ% -c mosquitto\mosquitto.conf -v"
start "Backend (FastAPI :8000)" cmd /k "cd /d %ROOT%backend && .venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 8000"
start "Dashboard (:5173)" cmd /k "cd /d %ROOT%dashboard && npm run dev -- --host"

timeout /t 6 /nobreak >nul
start "" http://localhost:5173
echo All services started in separate windows.
