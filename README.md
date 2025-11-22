# System Monitoring Dashboard

A lightweight, Linux system dashboard with a C++17 backend and a vanilla JS + ECharts frontend. It samples CPU, memory, disk, network, and process data from `/proc`, stores it in an in-memory ring buffer, and serves both a REST API and static single-page UI from the same binary.

## Project Overview
- Single binary that collects host metrics and exposes them via HTTP.
- Serves the bundled frontend (HTML/JS/CSS) alongside the API so the UI and backend stay in sync.
- Linux-focused (Debian/Ubuntu/Fedora tested) and built with CMake.

## Features
- Real-time CPU, memory, disk, and network charts with sliding windows.
- Process table with delta-aware refreshes to avoid redundant downloads.
- Incremental polling on the frontend to stay ~1s responsive while keeping bandwidth low.
- REST API and static assets served by the same process and port.

## Architecture / Components
- **Backend:** `main.cpp` builds the `dashboard` binary via `CMakeLists.txt`, wiring collectors, in-memory storage, and HTTP routes in `api/routes.cpp`.
- **Collectors & store:** `collector/` handles per-metric sampling from `/proc`; `store/` provides the ring buffer and system metadata.
- **Frontend assets:** `web/` contains `index.html`, `app.js`, and `styles.css`, mounted by the binary (default `WEB_ROOT=./web`).

## Screenshots

<p align="center">
  <img src="https://github.com/user-attachments/assets/73b0db9d-3561-4458-b173-4a90f9278a7f" width="320" />
  <img src="https://github.com/user-attachments/assets/4181e110-00ab-4455-b27e-4e569068c5d0" width="320" />
  <img src="https://github.com/user-attachments/assets/62086a68-3a55-4268-92d5-de74ce62591d" width="320" />
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/e68e15bc-44bc-4756-ae99-5944930e0331" width="320" />
  <img src="https://github.com/user-attachments/assets/45036d25-e576-4bd1-af91-ca17ed7d8dce" width="320" />
  <img src="https://github.com/user-attachments/assets/b621ad70-cb59-4345-848d-1037f848debb" width="320" />
</p>



## Prerequisites
Build tools on the Linux machine running the monitor:

**Debian / Ubuntu**
```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake
```

**Fedora / RHEL**
```bash
sudo dnf install -y gcc-c++ make cmake
```

## Installation
Clone the repository on the target Linux host:
```bash
git clone https://github.com/sebas-ib/system-monitoring-dashboard.git
cd system-monitoring-dashboard
```

## How to Build
From the repo root:
```bash
mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target dashboard -j"$(nproc)"
```
This produces the `dashboard` executable in `build/`.

## How to Run
### Local run (single process / single port)
Start the server from the build directory and point it at the `web` assets:
```bash
cd ~/system-monitoring-dashboard/build
WEB_ROOT=../web HOST_LABEL="$(hostname)" PORT=8080 ./dashboard
```
- `WEB_ROOT` – location of the static frontend files (The server looks for WEB_ROOT relative to the current working directory (default web). When running from build/, use WEB_ROOT=../web.).
- `HOST_LABEL` – label attached to exported metrics (defaults to the system hostname).
- `PORT` – TCP port to listen on (defaults to `8080`).

With the server running, open a browser on the same machine:
```text
http://localhost:8080
```
The UI lives at the root path, and API endpoints are under `/api/...`.

### Viewing the dashboard remotely via SSH tunnel
If the Linux machine is headless or remote, forward the port to your local machine (example values shown):
```bash
ssh -L 8080:localhost:8080 <user>@<server-ip>
```
Leave the SSH session open, then in your local browser visit:
```text
http://localhost:8080
```
Traffic flows from your laptop → SSH tunnel → `localhost:8080` on the Linux host where `dashboard` is running.

## Usage
- Browse to `http://<host>:<port>/` for the UI (or `?api=http://server:8080` to point the SPA at a different host).
- Key API endpoints implemented in `api/routes.cpp`:
  - `GET /api/info?key=system` — system metadata (hostname, cores, memory total, kernel, etc.).
  - `GET /api/status` — basic health and uptime.
  - `GET /api/metrics` — registry of metric names, units, and supported labels.
  - `GET /api/stored` — list of stored metric selectors and label dimensions.
  - `GET /api/query?metric=...&from=ms&to=ms[&labels=key:value]` — timeseries samples (vector series supported).
  - `GET /api/export?metric=...&from=ms&to=ms&format=csv|json[&labels=key:value&limit=n]` — export a series.
  - `GET /api/processes` — latest process snapshot.

## Notes / Limitations
- Linux-only: collectors depend on `/proc`; macOS collectors referenced in `CMakeLists.txt` are not present in this repository.
- Ensure firewalls expose the chosen `PORT` if accessing remotely.
