# System Monitoring Dashboard

A lightweight, Linux-first system dashboard with a C++17 backend and a vanilla JS + ECharts frontend. It samples CPU, memory, disk, network, and process data from `/proc`, stores it in an in-memory ring buffer, and exposes a small REST API alongside static web assets for a single-page UI.

## Features
- Real-time CPU, memory, disk, and network charts with sliding time windows.
- Incremental polling on the frontend to keep bandwidth low while remaining ~1s responsive.
- Process table with delta-aware refreshes to avoid redundant downloads.
- REST API served by the same binary that also mounts the static UI assets.
- Linux-focused collectors (Debian/Ubuntu tested) built with CMake.

## Repository layout & build entrypoints
- **Backend:** `main.cpp` builds the `dashboard` binary via `CMakeLists.txt`, wiring collectors, in-memory storage, and HTTP routes in `api/routes.cpp`.
- **Collectors & store:** `collector/` (per-metric sampling) and `store/` (ring buffer + system metadata).
- **Frontend assets:** `web/` contains `index.html`, `app.js`, and `styles.css` served by the binary (mount point defaults to `./web`).
- **Packaging:** `Dockerfile` (multi-stage runtime), `docker-compose.yml` example, and `packaging/systemd/system-monitoring-dashboard.service` for native installs.

## Screenshots
_Placeholder — drop your screenshots here (CPU dashboard, Memory tab, Process table, etc.)._

## Quickstart (Docker)
```bash
# Build the image
sudo docker build -t system-monitoring-dashboard .

# Run it (default PORT=8080; HOST_LABEL optional)
sudo docker run -it --rm -p 8080:8080 \
  -e HOST_LABEL="$(hostname)" \
  -e PORT=8080 \
  system-monitoring-dashboard
```
Open http://localhost:8080 in your browser (or use `?api=http://<host>:<port>` when accessing remotely).

### docker-compose
```bash
HOST_LABEL=$(hostname) docker compose up --build
```

## Quickstart (native Linux)
```bash
sudo apt-get update && sudo apt-get install -y build-essential cmake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Run from the repo root so WEB_ROOT=./web resolves
PORT=8080 HOST_LABEL="$(hostname)" ./build/dashboard
```
Then browse to http://localhost:8080 (or add `?api=http://server:8080` if the UI is opened from another machine).

## Configuration
- **PORT:** listening port for the HTTP server (default 8080).
- **HOST_LABEL:** label attached to exported metrics and shown in the UI; defaults to the system hostname.
- **WEB_ROOT:** path to static UI assets; defaults to `web` relative to the working directory.
- **Frontend API base URL:** resolved at runtime from `window.location` and can be overridden via `?api=http://host:port` in the browser address bar.

### Viewing another host's metrics
- Run the `dashboard` binary (or Docker container) on the target Linux machine and ensure its port is reachable (e.g., `-p 8080:8080` in Docker or an opened firewall rule).
- From any browser, navigate to the UI and point it at the remote API with `?api=http://<remote-host>:8080` (for example, `http://localhost:8080/?api=http://server:8080`). The SPA will poll the specified host for metrics while running locally.

## API overview
- `GET /api/info?key=system` — system metadata (hostname, cores, memory total, etc.).
- `GET /api/status` — service status and uptime.
- `GET /api/stored` — list of available metrics and label dimensions in the ring buffer.
- `GET /api/query?metric=...&from=ms&to=ms[&labels=key:value]` — timeseries samples (vector series supported).
- `GET /api/export?...` — export a series as CSV or JSON over a specified time window.
- `GET /api/processes[?since=ms]` — latest process snapshot, optionally delta-optimized with `since`.

## Deployment (non-Docker)
- **systemd:** copy `packaging/systemd/system-monitoring-dashboard.service` to `/etc/systemd/system/`, adjust `WorkingDirectory`, `ExecStart`, and environment values, then `systemctl enable --now system-monitoring-dashboard`.
- Ensure the working directory contains the `web/` assets (or set `WEB_ROOT` accordingly) so the UI is served alongside the API.

## License
Unless otherwise noted, this project is provided under the MIT license.

---
### How to try it in 60 seconds
```bash
# From a Linux shell with Docker available
cd system-monitoring-dashboard
sudo docker build -t system-monitoring-dashboard .
sudo docker run -p 8080:8080 system-monitoring-dashboard
# Open http://localhost:8080 in your browser
```