# Setting up the server on another Windows PC

The "server" PC runs three things: the MQTT broker (Mosquitto), the backend (FastAPI) and the dashboard.
The ESP32 and this PC must be on the same Wi-Fi/router.

## 1. Install the requirements
| Software | Version | Where |
|---|---|---|
| Git | any | https://git-scm.com/download/win |
| Python | 3.11 or newer (tick "Add python.exe to PATH") | https://www.python.org/downloads/ |
| Node.js | 20.19+ or 22+ (LTS) | https://nodejs.org |
| Mosquitto | 2.x, 64-bit | https://mosquitto.org/download/ |

The Mosquitto installer needs admin rights. Check afterwards in a new PowerShell: `git --version`, `python --version`, `node --version`.

## 2. Get the code
```powershell
cd C:\
git clone https://github.com/Vedant28082005/het_smart_agriculture.git
cd het_smart_agriculture
```
(Or copy the project folder over. Do not copy `node_modules` or `.venv`; they are rebuilt below.)

## 3. Give the PC a fixed IP
The ESP32 has this PC's IP written into its firmware, so it must not change.
Easiest: in the router's admin page, reserve (DHCP binding) an IP for this PC's MAC address.
Find the current IP with `ipconfig` (IPv4 of the Wi-Fi or Ethernet adapter). Call it `SERVER_IP` below.

## 4. Start the MQTT broker
One-time firewall rule (PowerShell as administrator):
```powershell
New-NetFirewallRule -DisplayName "Mosquitto MQTT" -Direction Inbound -Protocol TCP -LocalPort 1883 -Action Allow -Profile Private
```
Make sure the Windows network is set to "Private", otherwise this rule does not apply.

Run the broker (leave the window open):
```powershell
& "C:\Program Files\mosquitto\mosquitto.exe" -c mosquitto/mosquitto.conf -v
```
If the Mosquitto Windows service is also running, stop it first (`net stop mosquitto`, as administrator), or both will fight over port 1883.

## 5. Set up and start the backend
```powershell
cd backend
python -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt
copy .env.example .env
```
Edit `backend\.env`:
```
CORS_ORIGINS=http://localhost:5173,http://SERVER_IP:5173
MQTT_HOST=localhost
DEVICE_HOST=irrigation.local
DEVICE_API_KEY=esp32key        # must equal API_KEY in the ESP32's secrets.h
DEMO=0
```
If `irrigation.local` does not resolve on this PC, use the ESP32's IP instead (printed in the serial monitor as `WiFi up, ip=...`; reserve it in the router too).

Start it, listening on the network so other devices can use the API:
```powershell
.venv\Scripts\python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```
Check: http://localhost:8000/api/health. Optional: `.venv\Scripts\python -m pytest` runs the 17 backend tests.

## 6. Set up and start the dashboard
In a second PowerShell:
```powershell
cd C:\het_smart_agriculture\dashboard
npm install
copy .env.example .env
```
Edit `dashboard\.env`:
```
VITE_API_URL=http://SERVER_IP:8000
```
Start it for the whole network:
```powershell
npm run dev -- --host
```
Open http://SERVER_IP:5173 on this PC or any phone/laptop on the same Wi-Fi.

Allow the two ports through the firewall (administrator PowerShell, once) if other devices cannot open the page:
```powershell
New-NetFirewallRule -DisplayName "Irrigation web" -Direction Inbound -Protocol TCP -LocalPort 5173,8000 -Action Allow -Profile Private
```

## 7. Point the ESP32 at the new PC
Edit `firmware/irrigation/secrets.h` on the PC where you flash the ESP32:
```cpp
#define MQTT_HOST     "SERVER_IP"
```
Re-flash the sketch. The serial monitor should show `MQTT connected`, and the Mosquitto window shows the connection.
`secrets.h` is not in git, so create it from `secrets.example.h` on any PC you flash from.

## 8. Quick checks
| Check | Expected |
|---|---|
| `& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h localhost -t "farm/field1/#" -v` | `status online` and a `sensors` line every ~5 s |
| http://SERVER_IP:8000/api/latest | `"online": true` |
| Dashboard | live values, badge "Device online" |
| Press Auto/Manual or Pump in the dashboard | serial monitor on the ESP32 shows the change |

## Starting everything again later
Double-click `start_all.bat` in the project folder. It opens the three windows below (and installs dependencies on the first run). Or by hand:

Three windows: Mosquitto, backend (`uvicorn ...`), dashboard (`npm run dev -- --host`). The database file `backend\irrigation.db` keeps history between runs.

## Troubleshooting
- **ESP32 shows `MQTT connect failed rc=-2`**: broker not running, wrong `MQTT_HOST`, or firewall/Public network profile.
- **Dashboard says "Cannot reach the backend"**: wrong `VITE_API_URL`, backend not started with `--host 0.0.0.0`, or port 8000 blocked. Restart `npm run dev` after editing `.env`.
- **Browser console shows a CORS error**: add the exact address you typed in the browser to `CORS_ORIGINS` and restart the backend.
- **Commands fail with 504 "Device unreachable"**: the backend cannot reach the ESP32. Use its IP in `DEVICE_HOST`, and confirm `API_KEY` and `DEVICE_API_KEY` match.
- **Device badge says offline although the ESP32 is running**: no MQTT messages are arriving; check the `mosquitto_sub` command above.

This is a plain-HTTP, no-login setup meant for a trusted home/lab network. Do not expose ports 1883, 8000 or 5173 to the internet.
