const state = {
    metrics: [],
    presets: [],
    chart: null,
    currentMetric: null,
    currentUnit: '',
    autoRefreshTimer: null,
    isFetching: false
};

const dom = {
    status: document.getElementById('status'),
    metricSelect: document.getElementById('metricSelect'),
    timeRange: document.getElementById('timeRange'),
    labelInputs: document.getElementById('labelInputs'),
    refreshBtn: document.getElementById('refreshBtn'),
    chartTitle: document.getElementById('chartTitle'),
    chartSubtitle: document.getElementById('chartSubtitle'),
    unitBadge: document.getElementById('unitBadge'),
    lastUpdated: document.getElementById('lastUpdated'),
    chartCanvas: document.getElementById('metricChart'),
    chartEmpty: document.getElementById('chartEmpty'),
    chartError: document.getElementById('chartError'),
    autoToggle: document.getElementById('autoRefreshToggle'),
    autoPeriod: document.getElementById('autoRefreshPeriod'),
    exportLink: document.getElementById('exportLink')
};

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        init().catch(err => console.error('Failed to initialise dashboard', err));
    });
} else {
    init().catch(err => console.error('Failed to initialise dashboard', err));
}

async function init() {
    setupChart();
    attachListeners();
    await Promise.all([fetchStatus(), fetchMetrics(), fetchPresets()]);
    if (state.metrics.length) {
        dom.metricSelect.value = state.metrics[0].name;
        renderLabelInputs();
    }
    if (!dom.timeRange.value) {
        dom.timeRange.value = dom.timeRange.options[0]?.value ?? '20';
    }
    await refreshChart();
    handleAutoRefreshToggle();
}

function setupChart() {
    const context = dom.chartCanvas.getContext('2d');
    state.chart = new Chart(context, {
        type: 'line',
        data: {
            datasets: [
                {
                    label: 'value',
                    data: [],
                    tension: 0.25,
                    borderColor: '#2563eb',
                    borderWidth: 2,
                    backgroundColor: 'rgba(37, 99, 235, 0.18)',
                    fill: true,
                    pointRadius: 0,
                    spanGaps: true
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            scales: {
                x: {
                    type: 'time',
                    adapters: {
                        date: {
                            zone: Intl.DateTimeFormat().resolvedOptions().timeZone
                        }
                    },
                    time: {
                        tooltipFormat: 'PPpp'
                    },
                    ticks: {
                        maxRotation: 0,
                        autoSkip: true
                    },
                    grid: {
                        display: false
                    }
                },
                y: {
                    ticks: {
                        callback: (value) => formatValue(value, state.currentUnit)
                    },
                    grid: {
                        color: 'rgba(148, 163, 184, 0.15)'
                    }
                }
            },
            plugins: {
                legend: { display: false },
                tooltip: {
                    callbacks: {
                        label: (ctx) => formatValue(ctx.parsed.y, state.currentUnit, { withUnit: true })
                    }
                }
            }
        }
    });
}

function attachListeners() {
    dom.metricSelect.addEventListener('change', async () => {
        renderLabelInputs();
        await refreshChart();
    });

    dom.timeRange.addEventListener('change', refreshChart);
    dom.refreshBtn.addEventListener('click', refreshChart);

    dom.autoToggle.addEventListener('change', handleAutoRefreshToggle);
    dom.autoPeriod.addEventListener('change', () => {
        if (dom.autoToggle.checked) {
            handleAutoRefreshToggle();
        }
    });

    document.addEventListener('visibilitychange', () => {
        if (document.hidden) {
            stopAutoRefresh();
        } else if (dom.autoToggle.checked) {
            startAutoRefresh();
        }
    });
}

async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const body = await res.json();
        updateStatus('ok', `Up ${formatDuration(body.uptime_s ?? 0)}`);
    } catch (err) {
        updateStatus('error', 'Offline');
        console.error('Status fetch failed', err);
    }
}

async function fetchMetrics() {
    try {
        const res = await fetch('/api/metrics');
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const body = await res.json();
        state.metrics = Array.isArray(body.metrics) ? body.metrics.sort((a, b) => a.name.localeCompare(b.name)) : [];
        dom.metricSelect.innerHTML = '';
        state.metrics.forEach(metric => {
            const option = document.createElement('option');
            option.value = metric.name;
            option.textContent = `${metric.name} (${metric.unit})`;
            dom.metricSelect.appendChild(option);
        });
        if (!state.metrics.length) {
            const option = document.createElement('option');
            option.textContent = 'No metrics available';
            dom.metricSelect.appendChild(option);
            dom.metricSelect.disabled = true;
        }
    } catch (err) {
        console.error('Metric fetch failed', err);
        state.metrics = [];
        dom.metricSelect.innerHTML = '<option>Error loading metrics</option>';
        dom.metricSelect.disabled = true;
        showChartError('Failed to load metrics from the API.');
    }
}

async function fetchPresets() {
    const defaultPresets = [
        { name: 'Live (20s)', seconds: 20 },
        { name: '1 minute', seconds: 60 }
    ];
    try {
        const res = await fetch('/api/presets');
        if (res.ok) {
            const body = await res.json();
            if (Array.isArray(body.presets)) {
                state.presets = [...defaultPresets, ...body.presets];
            } else {
                state.presets = defaultPresets;
            }
        } else {
            state.presets = defaultPresets;
        }
    } catch (err) {
        console.warn('Presets fetch failed', err);
        state.presets = defaultPresets;
    }

    dom.timeRange.innerHTML = '';
    state.presets.forEach(preset => {
        const option = document.createElement('option');
        option.value = preset.seconds.toString();
        option.textContent = `${preset.name}`;
        dom.timeRange.appendChild(option);
    });
}

function renderLabelInputs() {
    dom.labelInputs.innerHTML = '';
    const selectedMetric = getSelectedMetric();
    if (!selectedMetric || !Array.isArray(selectedMetric.labels)) return;
    selectedMetric.labels.forEach(labelName => {
        const wrapper = document.createElement('div');
        wrapper.className = 'label-input';
        const input = document.createElement('input');
        input.type = 'text';
        input.placeholder = labelName === 'host' ? 'auto' : labelName;
        input.dataset.label = labelName;
        input.autocomplete = 'off';
        wrapper.appendChild(input);
        const hint = document.createElement('span');
        hint.className = 'meta';
        hint.textContent = labelName;
        wrapper.appendChild(hint);
        dom.labelInputs.appendChild(wrapper);
    });
}

function collectLabels() {
    const inputs = Array.from(dom.labelInputs.querySelectorAll('input[data-label]'));
    return inputs.reduce((acc, input) => {
        const value = input.value.trim();
        if (value.length) {
            acc[input.dataset.label] = value;
        }
        return acc;
    }, {});
}

async function refreshChart() {
    if (!state.metrics.length) return;
    if (state.isFetching) return;
    state.isFetching = true;
    dom.refreshBtn.disabled = true;
    dom.chartError.hidden = true;
    dom.chartEmpty.hidden = true;
    try {
        const metric = dom.metricSelect.value;
        const seconds = Number(dom.timeRange.value || 20);
        const labels = collectLabels();
        const now = Date.now();
        const from = now - seconds * 1000;
        const params = new URLSearchParams({
            metric,
            from: from.toString(),
            to: now.toString()
        });
        if (Object.keys(labels).length) {
            params.set('labels', Object.entries(labels).map(([k, v]) => `${k}:${v}`).join(','));
        }
        const query = params.toString();
        updateExportLink(query);
        const res = await fetch(`/api/timeseries?${query}`);
        if (!res.ok) {
            throw new Error(`API error ${res.status}`);
        }
        const body = await res.json();
        const samples = Array.isArray(body.samples) ? body.samples : [];
        const responseLabels = body.labels && typeof body.labels === 'object' ? body.labels : labels;
        state.currentMetric = metric;
        state.currentUnit = body.unit || '';
        updateChartMetadata(metric, responseLabels, state.currentUnit);
        updateChartDataset(samples);
    } catch (err) {
        console.error('Timeseries fetch failed', err);
        showChartError('Failed to load samples. Check the API status or adjust your selection.');
    } finally {
        state.isFetching = false;
        dom.refreshBtn.disabled = false;
        dom.lastUpdated.textContent = `Updated ${new Date().toLocaleTimeString()}`;
    }
}

function updateChartDataset(samples) {
    const dataset = state.chart.data.datasets[0];
    dataset.data = samples.map(([ts, value]) => ({ x: ts, y: value }));
    dom.chartEmpty.hidden = dataset.data.length !== 0;
    state.chart.update();
}

function updateChartMetadata(metricName, labels, unit) {
    dom.chartTitle.textContent = metricName;
    const labelObject = labels && typeof labels === 'object' ? labels : {};
    const labelEntries = Object.entries(labelObject);
    dom.chartSubtitle.textContent = labelEntries.length
        ? labelEntries.map(([k, v]) => `${k}=${v}`).join(', ')
        : 'No label filters (server defaults applied)';
    dom.unitBadge.textContent = unit || 'value';
}

function showChartError(message) {
    dom.chartError.textContent = message;
    dom.chartError.hidden = false;
    dom.chartEmpty.hidden = true;
    const dataset = state.chart.data.datasets[0];
    dataset.data = [];
    state.chart.update();
}

function handleAutoRefreshToggle() {
    if (dom.autoToggle.checked) {
        startAutoRefresh();
    } else {
        stopAutoRefresh();
    }
}

function startAutoRefresh() {
    stopAutoRefresh();
    const intervalSeconds = Math.max(2, Number(dom.autoPeriod.value) || 5);
    dom.autoPeriod.value = intervalSeconds;
    state.autoRefreshTimer = setInterval(() => {
        refreshChart();
    }, intervalSeconds * 1000);
}

function stopAutoRefresh() {
    if (state.autoRefreshTimer) {
        clearInterval(state.autoRefreshTimer);
        state.autoRefreshTimer = null;
    }
}

function updateStatus(kind, text) {
    dom.status.textContent = text;
    dom.status.classList.remove('status--pending', 'status--ok', 'status--error');
    dom.status.classList.add(`status--${kind}`);
}

function updateExportLink(query) {
    dom.exportLink.href = `/api/export?${query}&format=csv`;
}

function getSelectedMetric() {
    const current = dom.metricSelect.value;
    return state.metrics.find(m => m.name === current);
}

function formatValue(value, unit, options = {}) {
    if (value === null || value === undefined || Number.isNaN(value)) return '';
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) return String(value);
    const withUnit = Boolean(options.withUnit);
    if (typeof unit === 'string' && unit.includes('bytes')) {
        const perSecond = unit.includes('/sec');
        const pretty = formatBytes(numeric);
        const valueWithSuffix = perSecond ? `${pretty}/s` : pretty;
        return withUnit ? valueWithSuffix : valueWithSuffix;
    }
    if (unit === '%' || unit === 'pct') {
        const pretty = numeric.toFixed(1);
        return withUnit ? `${pretty} %` : pretty;
    }
    const formatter = Math.abs(numeric) >= 1000
        ? { maximumFractionDigits: 1 }
        : { maximumFractionDigits: 3 };
    const pretty = numeric.toLocaleString(undefined, formatter);
    return withUnit && unit ? `${pretty} ${unit}` : pretty;
}

function formatBytes(value) {
    const units = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'];
    let idx = 0;
    let val = value;
    while (Math.abs(val) >= 1024 && idx < units.length - 1) {
        val /= 1024;
        idx += 1;
    }
    const precision = idx === 0 ? 0 : 2;
    return `${val.toLocaleString(undefined, { maximumFractionDigits: precision })} ${units[idx]}`;
}

function formatDuration(seconds) {
    if (!Number.isFinite(seconds)) return '';
    if (seconds < 60) return `${seconds}s`;
    const mins = Math.floor(seconds / 60);
    const secs = Math.floor(seconds % 60);
    if (mins < 60) return `${mins}m ${secs}s`;
    const hours = Math.floor(mins / 60);
    const remainingMins = mins % 60;
    return `${hours}h ${remainingMins}m`;
}
