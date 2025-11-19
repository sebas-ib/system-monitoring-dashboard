// web/app.js — orchestrates dashboard charts, process table, and interactions.

// -------------------------
// Constants & State
// -------------------------

const API_BASE_URL = "http://192.168.73.143:8080";
const PROCESS_REFRESH_MS = 1000;
const CHART_REFRESH_MS = 1000;
const CORE_REFRESH_MS = 1000;

let cpuTotalChart;
let perCoreChart;
let TOTAL_MEM_BYTES = 0;
let ACTIVE_TAB = "cpu";

const MEMORY_DASHBOARD = {
    metrics: [],
    charts: new Map(),
    ready: false
};

const DISK_DASHBOARD = {
    metrics: [],
    charts: new Map(),
    ready: false
};

const NETWORK_DASHBOARD = {
    metrics: [],
    charts: new Map(),
    ready: false
};

const BASE_DATAZOOM = [
    { type: "inside", start: 70, end: 100, minSpan: 1 },
    { type: "slider", show: true, bottom: 25, height: 30, start: 70, end: 100, handleSize: 12, showDetail: true }
];

// -------------------------
// Utilities (formatters, conversions)
// -------------------------

function formatNumber(num, decimals = 2) {
    return Number(num.toFixed(decimals));
}

function bytesToMB(bytes) {
    return bytes / (1024 ** 2);
}

function bytesToGB(bytes) {
    return bytes / (1024 ** 3);
}

/**
 * Convert memory data points to the most readable unit.
 */
function convertMemoryDataset(dataPoints, totalBytes) {
    if (totalBytes >= 1024 ** 3) {
        return {
            data: dataPoints.map(value => formatNumber(bytesToGB(value), 2)),
            unit: "GB",
            yMax: formatNumber(bytesToGB(totalBytes), 2)
        };
    }

    return {
        data: dataPoints.map(value => formatNumber(bytesToMB(value), 1)),
        unit: "MB",
        yMax: formatNumber(bytesToMB(totalBytes), 1)
    };
}

const fmt = {
    pct: v => v == null ? "" : Number(v).toFixed(1),
    int: v => v == null ? "" : Math.trunc(v),
    num1: v => v == null ? "" : Number(v).toFixed(1),
    rss: v => v == null ? "" : Number(v).toFixed(3),
    str: v => v == null ? "" : String(v),
    hms: v => {
        if (v == null) return "";
        const s = Math.floor(Number(v));
        const h = Math.floor(s / 3600);
        const m = Math.floor((s % 3600) / 60);
        const sec = s % 60;
        return `${String(h).padStart(2, "0")}:${String(m).padStart(2, "0")}:${String(sec).padStart(2, "0")}`;
    }
};

// -------------------------
// API Requests
// -------------------------

async function fetchSystemInfo() {
    const response = await fetch(`${API_BASE_URL}/api/info?key=system`);
    return response.json();
}

async function fetchStoredMetrics() {
    const url = `${API_BASE_URL}/api/stored`;
    try {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        return await response.json();
    } catch (error) {
        console.error("Failed to load stored metrics:", error);
    }
}

async function fetchTimeseriesData({ metric, labels = null, from = 0, to = Date.now() }) {
    const query = new URLSearchParams({ metric, from: String(from), to: String(to) });

    if (labels && typeof labels === "object") {
        const labelPairs = Object.entries(labels)
            .map(([key, value]) => `${key}:${encodeURIComponent(String(value))}`)
            .join(",");
        if (labelPairs) query.set("labels", labelPairs);
    }

    const response = await fetch(`${API_BASE_URL}/api/query?${query.toString()}`);
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return response.json();
}

async function fetchProcessesSnapshot() {
    const response = await fetch(`${API_BASE_URL}/api/processes`);
    if (!response.ok) throw new Error("Failed to load processes");
    return response.json();
}

// -------------------------
// Data Processing
// -------------------------

async function initializeSystemMetadata() {
    const metadata = await fetchSystemInfo();
    TOTAL_MEM_BYTES = metadata.mem_total_bytes || 0;
}

/**
 * Normalize the API response into chart-ready payloads.
 * NOTE: no [unit] in the label here; we append final unit later.
 */
function buildSeriesPayload(info) {
    const samples = Array.isArray(info.samples)
        ? info.samples.slice().sort((a, b) => a[0] - b[0])
        : [];

    const labels = samples.map(([ts]) => new Date(ts).toLocaleTimeString());
    const data   = samples.map(([, v]) => v);
    const unit   = (info.unit || "").trim();
    const host   = info.labels?.host ? ` (${info.labels.host})` : "";

    const datasetLabel = `${info.metric}${host}`;
    return { labels, data, unit, datasetLabel };
}

/**
 * Build the per-core data series for the multi-series chart.
 */
function buildCoreSeries(samples) {
    if (!samples.length) {
        return { labels: [], series: [] };
    }

    const labels = samples.map(([timestamp]) => new Date(timestamp).toLocaleTimeString());
    const coreCount = samples[0][1].length;
    const series = [];

    for (let index = 0; index < coreCount; index++) {
        series.push({
            core: index,
            data: samples.map(([, values]) => values[index])
        });
    }

    return { labels, series };
}

// -------------------------
// Chart Engine
// -------------------------

function makeBaseOption(titleText = "") {
    return {
        animation: true,
        toolbox: { feature: { dataZoom: { yAxisIndex: "none" }, restore: {}, saveAsImage: {} } },
        title: { text: titleText },
        tooltip: { trigger: "axis" },
        grid: { left: 40, right: 20, top: 80, bottom: 80 },
        xAxis: { type: "category", data: [] },
        yAxis: { type: "value", min: 0, axisLabel: { formatter: value => value } },
        dataZoom: BASE_DATAZOOM,
        series: [{ name: titleText, type: "line", showSymbol: false, areaStyle: { opacity: 0.18 }, data: [] }]
    };
}

/**
 * Apply time-series data to a chart with auto-scaling.
 */
function renderTimeseriesChart(chart, payload) {
    if (!chart || !payload) return;

    let { labels, data, unit, datasetLabel } = payload;

    if (!data || data.length === 0) {
        // Nothing yet: keep axes but clear series
        chart.setOption({
            xAxis: { type: "category", data: labels || [] },
            series: [{ type: "line", data: [] }]
        });
        return;
    }

    // Detect before modifying datasetLabel
    const isPercentage   = unit === "%" || datasetLabel.includes("pct");
    const isMemoryMetric = datasetLabel.startsWith("mem.");
    const isDiskMetric   = datasetLabel.startsWith("disk.");
    const isNetMetric    = datasetLabel.startsWith("net.");
    const isThroughput   = isDiskMetric || isNetMetric; // bytes/sec → KB/s / MB/s

    let yMax = null;

    // MEMORY AUTO-SCALING
    if (isMemoryMetric && TOTAL_MEM_BYTES > 0) {
        const converted = convertMemoryDataset(data, TOTAL_MEM_BYTES);
        data = converted.data;
        unit = converted.unit;
        yMax = converted.yMax;
        datasetLabel = datasetLabel.replace(/_bytes$/, "");
    }

    // DISK / NET AUTO-SCALING (bytes/sec)
    if (isThroughput) {
        const maxVal = Math.max(...data);

        if (maxVal > 1024 * 1024) {
            data = data.map(v => formatNumber(v / (1024 * 1024), 2));
            unit = "MB/s";
        } else if (maxVal > 1024) {
            data = data.map(v => formatNumber(v / 1024, 2));
            unit = "KB/s";
        } else {
            unit = "B/s";
        }

        yMax = Math.max(...data) * 1.1;
        if (yMax === 0) yMax = 1;
    }

    // PERCENTAGE AUTO-SCALING
    if (isPercentage) {
        yMax = 100;
    }

    // GENERIC AUTO-SCALING
    if (yMax == null) {
        const maxValue = Math.max(...data);
        yMax = maxValue * 1.1;
        if (yMax === 0) yMax = 1;
    }

    const titleUnit = !isPercentage && unit ? ` [${unit}]` : "";

    chart.setOption({
        title: { text: datasetLabel + titleUnit, left: "center" },
        tooltip: {
            trigger: "axis",
            formatter: params => {
                const value = params[0].value;
                const label = params[0].axisValue;

                if (isPercentage) {
                    return `${label}<br>${formatNumber(value, 2)}%`;
                }
                return `${label}<br>${formatNumber(value, 2)} ${unit}`;
            }
        },
        xAxis: { type: "category", data: labels },
        yAxis: {
            type: "value",
            min: 0,
            max: yMax,
            axisLabel: {
                formatter: v => isPercentage ? `${v}%` : `${v} ${unit}`
            }
        },
        series: [{
            type: "line",
            smooth: false,
            showSymbol: false,
            data
        }]
    }, false, false, ["series", "xAxis", "yAxis", "title"]);
}

/**
 * Keeps track of metrics and refreshes them together.
 */
class ChartRegistry {
    constructor() {
        this.items = [];
    }

    registerChart({ chart, metric, labels = null, title = metric }) {
        chart.setOption(makeBaseOption(title));
        const refresh = async () => {
            try {
                const info = await fetchTimeseriesData({ metric, labels });
                const payload = buildSeriesPayload(info);
                renderTimeseriesChart(chart, payload);
            } catch (_) { /* swallow to keep polling */ }
        };
        this.items.push({ refresh });
        return refresh;
    }

    async refreshAll() {
        for (const item of this.items) {
            await item.refresh();
        }
    }
}

const CPU_REGISTRY  = new ChartRegistry();
const MEM_REGISTRY  = new ChartRegistry();
const DISK_REGISTRY = new ChartRegistry();
const NET_REGISTRY  = new ChartRegistry();

// -------------------------
// UI Rendering - CPU
// -------------------------

function initCpuTotalChart() {
    const container = document.getElementById("chart-obj");
    cpuTotalChart = echarts.init(container);
    CPU_REGISTRY.registerChart({ chart: cpuTotalChart, metric: "cpu.total_pct", title: "cpu.total_pct" });
}

function initPerCoreChart() {
    const container = document.getElementById("chart-cores");
    if (!container) return;

    perCoreChart = echarts.init(container);
    perCoreChart.setOption({
        ...makeBaseOption("cpu.core_pct (per-core)"),
        legend: { top: 40 },
        yAxis: { type: "value", min: 0, max: 100, axisLabel: { formatter: value => `${value}%` } },
        series: [],
        tooltip: {
            trigger: "axis",
            formatter: params => {
                const time = params[0].axisValue;
                let html = `${time}<br>`;
                for (const p of params) {
                    const value = formatNumber(p.value, 2);
                    html += `
                <span style="display:inline-block;margin-right:5px;
                             border-radius:50%;width:10px;height:10px;
                             background:${p.color};"></span>
                ${p.seriesName}: ${value}%<br>`;
                }
                return html;
            }
        }
    });

    window.addEventListener("resize", () => perCoreChart && perCoreChart.resize());
}

function renderPerCoreChart(labels, coreSeries) {
    if (!perCoreChart) return;

    perCoreChart.setOption({
        xAxis: { data: labels },
        series: coreSeries.map(seriesItem => ({
            name: `core ${seriesItem.core}`,
            type: "line",
            showSymbol: false,
            areaStyle: { opacity: 0.18 },
            data: seriesItem.data
        })),
        legend: { data: coreSeries.map(seriesItem => `core ${seriesItem.core}`) }
    }, false, false, ["series", "xAxis", "legend"]);
}

async function loadAndRenderPerCoreData() {
    const info = await fetchTimeseriesData({ metric: "cpu.core_pct", from: 0, to: Date.now() });
    if (!info.vector) return;
    const samples = info.samples || [];
    if (!samples.length) return;
    const { labels, series } = buildCoreSeries(samples);
    renderPerCoreChart(labels, series);
}

// -------------------------
// UI Rendering - Disk
// -------------------------

async function setupDiskCharts() {
    if (DISK_DASHBOARD.ready) return;

    const stored = await fetchStoredMetrics();
    if (!stored || !Array.isArray(stored.metrics)) return;

    const diskEntries = stored.metrics
        .filter(m => m.name.startsWith("disk."))
        .map(m => ({
            metric: m.name,
            devices: (m.labels?.dev || [])
        }));

    DISK_DASHBOARD.metrics = diskEntries;

    const wrapper = document.getElementById("disk-charts");
    if (!wrapper) return;

    diskEntries.forEach(entry => {
        entry.devices.forEach(deviceName => {
            const chartId = `${entry.metric}-${deviceName}`.replace(/[^\w-]/g, "_");

            const container = document.createElement("div");
            container.className = "disk-chart";
            container.id = chartId;
            wrapper.appendChild(container);

            const chart = echarts.init(container);
            DISK_REGISTRY.registerChart({
                chart,
                metric: entry.metric,
                labels: { dev: deviceName },
                title: `${entry.metric} (${deviceName})`
            });

            DISK_DASHBOARD.charts.set(chartId, chart);
        });
    });

    window.addEventListener("resize", () => {
        DISK_DASHBOARD.charts.forEach(ch => ch.resize());
    });

    DISK_DASHBOARD.ready = true;
}

// -------------------------
// UI Rendering - Memory
// -------------------------

async function setupMemoryCharts() {
    if (MEMORY_DASHBOARD.ready) return;

    const stored = await fetchStoredMetrics();
    if (!stored || !Array.isArray(stored.metrics)) return;

    MEMORY_DASHBOARD.metrics = stored.metrics
        .map(metric => metric.name)
        .filter(name => typeof name === "string" && name.startsWith("mem"))
        .sort();

    const wrapper = document.getElementById("mem-charts");
    if (!wrapper) return;

    MEMORY_DASHBOARD.metrics.forEach(metric => {
        const chartId = `mem-chart-${metric.replace(/[^\w-]+/g, "_")}`;
        const container = document.createElement("div");
        container.className = "mem-chart";
        container.id = chartId;
        wrapper.appendChild(container);

        const chart = echarts.init(container);
        MEM_REGISTRY.registerChart({ chart, metric, title: metric });
        MEMORY_DASHBOARD.charts.set(metric, chart);
    });

    window.addEventListener("resize", () => MEMORY_DASHBOARD.charts.forEach(chart => chart.resize()));
    MEMORY_DASHBOARD.ready = true;
}

// -------------------------
// UI Rendering - Network
// -------------------------

async function setupNetworkCharts() {
    if (NETWORK_DASHBOARD.ready) return;

    const stored = await fetchStoredMetrics();
    if (!stored || !Array.isArray(stored.metrics)) return;

    const netEntries = stored.metrics
        .filter(m => m.name.startsWith("net."))
        .map(m => ({
            metric: m.name,
            ifaces: (m.labels?.iface || [])
        }));

    NETWORK_DASHBOARD.metrics = netEntries;

    const wrapper = document.getElementById("net-charts");
    if (!wrapper) return;

    netEntries.forEach(entry => {
        entry.ifaces.forEach(ifaceName => {
            const chartId = `${entry.metric}-${ifaceName}`.replace(/[^\w-]/g, "_");

            const container = document.createElement("div");
            container.className = "net-chart";
            container.id = chartId;
            wrapper.appendChild(container);

            const chart = echarts.init(container);
            NET_REGISTRY.registerChart({
                chart,
                metric: entry.metric,
                labels: { iface: ifaceName },
                title: `${entry.metric} (${ifaceName})`
            });

            NETWORK_DASHBOARD.charts.set(chartId, chart);
        });
    });

    window.addEventListener("resize", () => {
        NETWORK_DASHBOARD.charts.forEach(ch => ch.resize());
    });

    NETWORK_DASHBOARD.ready = true;
}

// -------------------------
// Processes table
// -------------------------

function renderProcessTable(rows) {
    const tbody = document.querySelector("#proc-table tbody");
    const fragment = document.createDocumentFragment();
    rows.forEach(process => {
        const tr = document.createElement("tr");
        tr.innerHTML = `
          <td class="num">${fmt.int(process.pid)}</td>
          <td title="${process.name}">${fmt.str(process.name)}</td>
          <td class="num">${fmt.pct(process.cpu_pct)}</td>
          <td class="num">${fmt.hms(process.cpu_time_s)}</td>
          <td class="num">${fmt.num1(process.idle_wakeups_per_s)}</td>
          <td class="num">${fmt.pct(process.mem_pct)}</td>
          <td class="num">${fmt.int(process.nice)}</td>
          <td class="num">${fmt.int(process.ppid)}</td>
          <td class="num">${fmt.int(process.priority)}</td>
          <td class="num">${fmt.rss(process.rss_mb)}</td>
          <td class="state">${fmt.str(process.state)}</td>
          <td class="num">${fmt.int(process.threads)}</td>
          <td class="user">${fmt.str(process.user)}</td>`;
        fragment.appendChild(tr);
    });
    tbody.replaceChildren(fragment);
}

async function loadProcesses() {
    try {
        const rows = await fetchProcessesSnapshot();
        renderProcessTable(rows);
    } catch (_) {
        document.querySelector("#proc-table tbody").innerHTML =
            `<tr><td colspan="13">Failed to load /api/processes</td></tr>`;
    }
}

function enableProcessTableColumnResize() {
    const table = document.getElementById("proc-table");
    if (!table) return;
    const cols = table.querySelectorAll("colgroup col");
    const headerCells = table.tHead?.rows?.[0]?.cells || [];

    [...headerCells].forEach((th, index) => {
        const grip = th.querySelector(".grip");
        if (!grip) return;
        let startX;
        let startWidth;

        const onMove = event => {
            const deltaX = (event.clientX || event.touches?.[0]?.clientX) - startX;
            const width = Math.max(50, startWidth + deltaX);
            cols[index].style.width = `${width}px`;
        };
        const onUp = () => {
            window.removeEventListener("mousemove", onMove);
            window.removeEventListener("mouseup", onUp);
            window.removeEventListener("touchmove", onMove);
            window.removeEventListener("touchend", onUp);
        };
        grip.addEventListener("mousedown", event => {
            startX = event.clientX;
            startWidth = th.getBoundingClientRect().width;
            window.addEventListener("mousemove", onMove);
            window.addEventListener("mouseup", onUp);
            event.preventDefault();
        });
        grip.addEventListener("touchstart", event => {
            startX = event.touches[0].clientX;
            startWidth = th.getBoundingClientRect().width;
            window.addEventListener("touchmove", onMove, { passive: false });
            window.addEventListener("touchend", onUp);
            event.preventDefault();
        }, { passive: false });
    });
}

// -------------------------
// Tabs & time range
// -------------------------

function activatePanel(panelId, tabBtn) {
    document.querySelectorAll(".panel").forEach(panel => panel.classList.add("hidden"));
    document.getElementById(panelId)?.classList.remove("hidden");

    document.querySelectorAll(".tab-btn").forEach(btn =>
        btn.setAttribute("aria-selected", "false")
    );
    tabBtn.setAttribute("aria-selected", "true");

    ACTIVE_TAB =
        panelId === "panel-cpu"  ? "cpu"  :
            panelId === "panel-mem"  ? "mem"  :
                panelId === "panel-disk" ? "disk" :
                    "net";
}

function wireTabs() {
    const cpuBtn  = document.getElementById("tab-cpu");
    const memBtn  = document.getElementById("tab-mem");
    const diskBtn = document.getElementById("tab-disk");
    const netBtn  = document.getElementById("tab-net");

    cpuBtn?.addEventListener("click", () =>
        activatePanel("panel-cpu", cpuBtn)
    );

    memBtn?.addEventListener("click", async () => {
        activatePanel("panel-mem", memBtn);
        await setupMemoryCharts();
        await MEM_REGISTRY.refreshAll();
    });

    diskBtn?.addEventListener("click", async () => {
        activatePanel("panel-disk", diskBtn);
        await setupDiskCharts();
        await DISK_REGISTRY.refreshAll();
    });

    netBtn?.addEventListener("click", async () => {
        activatePanel("panel-net", netBtn);
        await setupNetworkCharts();
        await NET_REGISTRY.refreshAll();
    });
}

function selectTimeFrame(seconds) {
    window.TIME_WINDOW_S = Number(seconds);

    if (ACTIVE_TAB === "cpu") {
        CPU_REGISTRY.refreshAll();
        loadAndRenderPerCoreData();
    } else if (ACTIVE_TAB === "mem") {
        MEM_REGISTRY.refreshAll();
    } else if (ACTIVE_TAB === "disk") {
        DISK_REGISTRY.refreshAll();
    } else if (ACTIVE_TAB === "net") {
        NET_REGISTRY.refreshAll();
    }
}
window.selectTimeFrame = selectTimeFrame;

// -------------------------
// Bootstrapping
// -------------------------

document.addEventListener("DOMContentLoaded", async () => {
    await initializeSystemMetadata();

    initCpuTotalChart();
    initPerCoreChart();
    wireTabs();
    enableProcessTableColumnResize();

    loadProcesses();
    await loadAndRenderPerCoreData();

    setInterval(loadProcesses, PROCESS_REFRESH_MS);

    setInterval(() => {
        if (ACTIVE_TAB === "cpu") {
            CPU_REGISTRY.refreshAll();
            loadAndRenderPerCoreData();
        }
    }, CHART_REFRESH_MS);

    setInterval(() => {
        if (ACTIVE_TAB === "mem") {
            MEM_REGISTRY.refreshAll();
        }
    }, CHART_REFRESH_MS);

    setInterval(() => {
        if (ACTIVE_TAB === "disk") {
            DISK_REGISTRY.refreshAll();
        }
    }, CHART_REFRESH_MS);

    setInterval(() => {
        if (ACTIVE_TAB === "net") {
            NET_REGISTRY.refreshAll();
        }
    }, CHART_REFRESH_MS);
});
