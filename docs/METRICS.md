## Purpose

Defines which metrics are collected and stored, how they are named, labeled, and sampled.

### Naming Convention

`<category>.<metric_name>{<label_key>=<label_value>,...}`

- Lowercase with dots
- Lables to identify sub resources
- Units must be consistent with all samples

### Core Metrics (v0)

| Category                | Metric Name        | Description                             | Unit      | Labels          | Example                             |
|-------------------------|--------------------|-----------------------------------------|-----------|-----------------|-------------------------------------|
| **CPU**                 | `cpu.total_pct`    | Overall CPU utilization (user + system) | %         | host            | `cpu.total_pct{host=macbook}`       |
|                         | `cpu.core_pct`     | Per-core CPU utilization                | %         | host, core      | `cpu.core_pct{host=macbook,core=3}` |
| **Memory**              | `mem.used_bytes`   | Used memory (total - free)              | bytes     | host            |                                     |
|                         | `mem.free_bytes`   | Free memory available                   | bytes     | host            |                                     |
| **Disk I/O**            | `disk.read_bytes`  | Bytes read per interval                 | bytes/sec | host, dev       | `disk.read_bytes{dev=sda}`          |
|                         | `disk.write_bytes` | Bytes written per interval              | bytes/sec | host, dev       | `disk.write_bytes{dev=sda}`         |
|                         | `disk.util_pct`    | Disk utilization percentage (optional)  | %         | host, dev       |                                     |
| **Network**             | `net.rx_bytes`     | Bytes received per interval             | bytes/sec | host, iface     | `net.rx_bytes{iface=en0}`           |
|                         | `net.tx_bytes`     | Bytes transmitted per interval          | bytes/sec | host, iface     | `net.tx_bytes{iface=en0}`           |
| **Processes (summary)** | `proc.count`       | Total running processes                 | count     | host            |                                     |
|                         | `proc.top`         | Top N processes by CPU% and RSS         | table     | host, pid, comm | (returned via API table)            |


### Future Metrics
CPU:
  cpu.total_pct
  cpu.core_pct{core}
  cpu.load1, cpu.load5, cpu.load15
Memory:
  mem.used_bytes (wired+active+compressed)
  mem.cached_bytes (inactive(+speculative))
  mem.available_bytes
  swap.used_bytes
  mem.pageins_per_s, mem.pageouts_per_s
Disk:
  disk.read_bytes_per_s{dev}
  disk.write_bytes_per_s{dev}
Network:
  net.rx_bytes_per_s{iface}
  net.tx_bytes_per_s{iface}
Processes (table, not timeseries at first):
  pid, comm, cpu_pct, rss_bytes, threads

### Sampling Strategy

| Tier            | Interval | Purpose                                                                |
|-----------------|----------|------------------------------------------------------------------------|
| **Raw**         | 1 s      | Used for live charts and recent detail (≈ 2 hours kept)                |
| **10 s rollup** | 10 s     | Smoothed data for standard dashboard views (≈ 24 hours kept)           |
| **60 s rollup** | 60 s     | Longer retention for trend comparison (≈ 7 days kept, optional for v0) |

Collectors append samples in 1 s intervals and trigger rollups in background threads.

### Label Guidelines

- Keep label cardinality low
    - **Allowed labels:** `host`, `core`, `dev`, `iface`, `pid`, `comm`

### Derived Metrics (v1 ideas)

- `cpu.idle_pct = 100 - cpu.total_pct`
- `mem.used_pct = mem.used_bytes / (mem.used_bytes + mem.free_bytes) * 100`
- `net.util_pct = (rx + tx) / link_speed_bytes * 100`
- `disk.iops = (read_ops + write_ops) / sec`

These can be computed on query, not stored
