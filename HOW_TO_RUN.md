# How to Run

This project is a C++ monitoring daemon that:

- samples CPU / memory / disk / network metrics on a Linux host
- exposes them via a REST API
- serves a single-page web dashboard (HTML/JS) from the same process

You can run it:

- **directly on a Linux machine** (with or without a GUI)
- and (optionally) **view the dashboard remotely** via SSH tunneling

---

## 1. Prerequisites

On the **Linux machine** where you want to run the monitor:

### System packages

Debian / Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake
```

Fedora / RHEL:

```bash
sudo dnf install -y gcc-c++ make cmake
```

### Clone this repo

```bash
git clone https://github.com/sebas-ib/system-monitoring-dashboard.git
cd system-monitoring-dashboard
```

---

## 2. Build the dashboard binary

From the repo root on the Linux machine:

```bash
cd system-monitoring-dashboard

mkdir -p build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target dashboard -j"$(nproc)"
```

This produces the `dashboard` executable in `build/`.

Directory layout on the Linux box should look like:

```text
system-monitoring-dashboard/
  ├─ web/          # frontend (index.html, JS, CSS)
  ├─ build/        # C++ binary (dashboard)
  └─ ...
```

---

## 3. Run on a Linux machine (single process / single port)

### If you’re running from `build/`

Start the server and point it at the `web` directory one level up:

```bash
cd ~/system-monitoring-dashboard/build

WEB_ROOT=../web HOST_LABEL="$(hostname)" PORT=8080 ./dashboard
```

- `WEB_ROOT` – where the static files live (the `web` folder).
- `HOST_LABEL` – label used in metrics (defaults to system hostname if not set).
- `PORT` – TCP port to listen on (defaults to `8080` if unset).

Once running, the server will:

- serve the web UI at `http://<host>:8080/`
- expose API endpoints under `http://<host>:8080/api/...`

### If the Linux machine has a GUI

Just open a browser **on that machine** and visit:

```text
http://localhost:8080
```

You should see the dashboard.

---

## 4. Viewing the dashboard remotely via SSH tunnel (e.g. from a Mac)

If the Linux machine is **headless** or on a different box (VM, server, etc.), you can view the dashboard in your local browser via SSH port forwarding.

Assume:

- Linux machine username: `sebastian`
- Linux machine IP: `192.168.73.144` (replace with your actual IP)
- `dashboard` is already running on port `8080` (see step 3)

### Step 1 – Open an SSH tunnel from your laptop

On your **laptop** (e.g., Mac):

```bash
ssh -L 8080:localhost:8080 sebastian@192.168.73.144
```

Leave this SSH session open.

### Step 2 – Open the dashboard in your laptop’s browser

On your laptop:

```text
http://localhost:8080
```

Your browser will now show the dashboard for the **remote** Linux machine:

- `localhost:8080` on your laptop
- → forwarded via SSH
- → `localhost:8080` on the Linux machine (where `dashboard` is running)