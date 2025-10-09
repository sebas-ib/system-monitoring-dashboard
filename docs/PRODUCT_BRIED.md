## Problem

Local single-node observability for essential system metrics: CPU, memory, disk I/O, and network usage.

Devs often lack a lightweight, real-time wat to see how a program or embedded system behaves under load.
This dashbpard provides a way for the host machine to collect and display data.

### Target Users

- Primary: Developer running the experiments or tests
- Secondary: Teammates obesrving the same device or server on a shared LAN

Access is through any modern browser by visiting the host machine's IP and port. No auth needed.

### MVP

- Single machine only
- Collects and stores a small set of metrics at fixed intervals
- Exposes a REST API for metrics and queries
- Displays a simple web dashboard with charts
