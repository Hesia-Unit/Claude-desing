# HESIA Server UI (eDEX-inspired)

This UI adds a full command console for the HESIA server. It shows:
- Jetson vitals (CPU temp/usage, RAM, voltage, current, power)
- Drone GPS (simulated constant coordinates)
- Live decrypted frames (30 FPS refresh)
- File tree (forensic directory)
- Terminal log view with severity colors

## 1) Build server (WSL)
```bash
cd /mnt/c/Users/matis/Documents/Hesia/Hesia-Simulation/AIR/SOL/Serveur
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 2) Run server
```bash
sudo ./build/hesia_server_cp
```

## 3) Prepare UI data directory
The server writes UI artifacts to `/var/log/hesia/ui` by default:
```bash
sudo install -d -m 755 /var/log/hesia/ui
```

Optional policy override:
```
server.ui_dir=/var/log/hesia/ui
```
(If you add this to policy.conf, re-sign policy.sig.)

## 4) Start UI server
```bash
cd /mnt/c/Users/matis/Documents/Hesia/Hesia-Simulation/AIR/SOL/Serveur
python3 tools/ui_server.py
```

Environment overrides:
```bash
HESIA_UI_PORT=8080 \
HESIA_UI_DATA_DIR=/var/log/hesia/ui \
HESIA_LOG_FILE=/var/log/hesia/HESIA-SERVER-CPP.log \
HESIA_UI_TREE_ROOT=/var/log/hesia \
python3 tools/ui_server.py
```

Open: `http://localhost:8080`

## 4b) Run as a desktop app (optional)
This wraps the UI in a native window using PyWebView.
```bash
cd /mnt/c/Users/matis/Documents/Hesia/Hesia-Simulation/AIR/SOL/Serveur
python3 -m pip install pywebview
python3 tools/ui_app.py
```
Window sizing overrides:
```
HESIA_UI_WIDTH=1280 HESIA_UI_HEIGHT=720 python3 tools/ui_app.py
```

## 5) Drone telemetry (Jetson)
The drone now sends TELEMETRY once per second. Rebuild and deploy the drone binary.
The GPS position is simulated with constant coordinates (Paris by default).

## Notes
- The audit log is binary/encrypted. The UI uses server log files for readable output.
- Live video comes from decrypted JPEG frames written to `latest.jpg` by the server.
- If the UI shows `--`, check that telemetry is arriving and the UI data dir is writable.
