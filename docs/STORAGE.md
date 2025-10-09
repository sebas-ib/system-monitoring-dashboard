## Purpose

Defines how system emtrics are persisted, rolled up, and compacted over time. The storage layer should be simple,
reliable, and easily queryable from both the API an CLI tools.

### Design Goals

- Lightweight: single file database or simple binary log
- Durable: safe against crashes
- Queryable: efficient queries by metric, label, and time
- Portable: no external dependencies

## Data Model

### Tables

| Table       | Purpose                                | Key Columns                                                                                                     |
|-------------|----------------------------------------|-----------------------------------------------------------------------------------------------------------------|
| **metrics** | Registry of all metric definitions     | `id INTEGER PRIMARY KEY`, `name TEXT UNIQUE`, `unit TEXT`, `description TEXT`                                   |
| **series**  | Distinguishes each labeled time-series | `id INTEGER PRIMARY KEY`, `metric_id INTEGER`, `labels_hash TEXT`, `labels_json TEXT`, `FOREIGN KEY(metric_id)` |
| **samples** | Raw and rolled-up metric values        | `series_id INTEGER`, `ts_ms INTEGER`, `value REAL`, `bucket INTEGER`, `rollup TEXT`                             |

### Sample Row Example

| series_id | ts_ms         | value | bucket     | rollup |
|-----------|---------------|-------|------------|--------|
| 42        | 1738723551000 | 17.3  | 1738723551 | "raw"  |

### Rollup levels

| Rollup | Interval | Aggregations       | Stored As      |
|--------|----------|--------------------|----------------|
| `raw`  | 1 s      | mean(value)        | direct samples |
| `10s`  | 10 s     | min, max, avg, p95 | rollup         |
| `60s`  | 60 s     | min, max, avg, p95 | rollup         |

Each higher rollup is computed periodically by scanning recent lower-tier data.

### Retention Policy

| Rollup | Keep For                 | Action After Expiry         |
|--------|--------------------------|-----------------------------|
| `raw`  | 2 hours                  | downsample to `10s` rollups |
| `10s`  | 24 hours                 | downsample to `60s` rollups |
| `60s`  | 7 days (optional for v0) | delete or archive           |