## Purpose

Define the REST API surface between the backend and the web dashboard (or any client).  
The API exposes metric definitions, time-series data, and export functions, forming the bridge between the data store
and visualization layer.

## General Principles

- **Stateless:** Every request is independent; no sessions or cookies.
- **JSON-first:** All structured responses are JSON (`application/json`).
- **Predictable URLs:** Hierarchical, minimal query parameters.
- **CORS-enabled:** Allows access from any browser within LAN.

## Base URL

Default port: `8080`  
Example: `http://localhost:8080/api`

## Endpoints Overview

| Method | Endpoint      | Purpose                                      |
|--------|---------------|----------------------------------------------|
| `GET`  | `/metrics`    | List all available metrics and their labels  |
| `GET`  | `/timeseries` | Query metric data for a time window          |
| `GET`  | `/export`     | Download metric data as CSV/JSON             |
| `GET`  | `/status`     | Health check for the API server              |
| `GET`  | `/presets`    | Predefined time windows (e.g., 15m, 1h, 24h) |

## `/metrics`

Returns the registry of metrics known to the store.

**Example Request**

```
GET /api/metrics
```

**Example Response**

```json
{
  "metrics": [
    {
      "name": "cpu.total_pct",
      "unit": "%",
      "labels": [
        "host"
      ]
    },
    {
      "name": "mem.used_bytes",
      "unit": "bytes",
      "labels": [
        "host"
      ]
    },
    {
      "name": "net.rx_bytes",
      "unit": "bytes/sec",
      "labels": [
        "host",
        "iface"
      ]
    }
  ]
}
```

**Status Codes**

- `200 OK` — success
- `500 Internal Server Error` — store not available

## `/timeseries`

Returns a JSON array of samples for a given metric and time window.

**Required Query Parameters**

| Parameter | Type    | Description                        |
|-----------|---------|------------------------------------|
| `metric`  | string  | Metric name (e.g. `cpu.total_pct`) |
| `from`    | integer | Start time (epoch ms)              |
| `to`      | integer | End time (epoch ms)                |

**Optional Parameters**

| Parameter | Type    | Description                                                         |
|-----------|---------|---------------------------------------------------------------------|
| `step`    | integer | Desired sampling step in seconds (default auto)                     |
| `labels`  | string  | Label filters in `key:value` form, comma-separated (e.g., `core:2`) |
| `rollup`  | string  | One of `raw`, `10s`, `60s`, or `auto`                               |
| `limit`   | integer | Max samples to return (default 10,000)                              |

**Example Request**

```
GET /api/timeseries?metric=cpu.total_pct&from=1738710000000&to=1738720000000&rollup=10s
```

**Example Response**

```json
{
  "metric": "cpu.total_pct",
  "unit": "%",
  "rollup": "10s",
  "labels": {
    "host": "macbook"
  },
  "samples": [
    [
      1738710000000,
      17.3
    ],
    [
      1738710010000,
      18.6
    ],
    [
      1738710020000,
      16.9
    ]
  ]
}
```

**Status Codes**

- `200 OK` success
- `400 Bad Request` missing/invalid parameters
- `422 Unprocessable Entity` unknown metric or label mismatch
- `500 Internal Server Error` store query failure

---

## `/export`

Exports the same data returned by `/timeseries` in file format.

**Required Parameters**
Same as `/timeseries`, plus one of:

- `format=json`
- `format=csv`

**Example Request**

```
GET /api/export?metric=mem.used_bytes&from=1738710000000&to=1738720000000&format=csv
```

**Example CSV Output**

```
timestamp,value
1738710000000,2313047040
1738710010000,2314485760
1738710020000,2314977280
```

**Status Codes**

- `200 OK` file stream
- `400 Bad Request` invalid parameters
- `500 Internal Server Error` export failure

---

## `/status`

Simple endpoint to check system health.

**Example Response**

```json
{
  "status": "ok",
  "uptime_s": 5320,
  "metrics_collected": 6,
  "store_size_mb": 12.3
}
```

---

## `/presets`

Returns standard dashboard time windows.

**Example Response**

```json
{
  "presets": [
    {
      "name": "15m",
      "seconds": 900
    },
    {
      "name": "1h",
      "seconds": 3600
    },
    {
      "name": "24h",
      "seconds": 86400
    }
  ]
}
```

---

## Error Format (All Endpoints)

Errors always return structured JSON:

```json
{
  "error": {
    "code": 400,
    "message": "Missing required parameter 'metric'"
  }
}
```

**Dashboard**

- `GET /metrics` populate dropdowns
- `GET /timeseries?...` render charts
- `GET /status` show connection health indicator

**CSV Export**

- `GET /export?...&format=csv` trigger file download

**Custom Script**

```bash
curl "http://localhost:8080/api/timeseries?metric=cpu.total_pct&from=...&to=..." | jq '.'
```

