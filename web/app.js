
let myChart;
let myCoresChart;
let HOST = '';
let CORES = [];
let TOTAL_MEM_BYTES = 0;


const MEM = {
    metrics: [],             // names like mem.free_mb, mem.used_pct, ...
    charts: new Map(),       // metricName -> echartsInstance
    ready: false
};

async function getSystemInfo() {
    const r = await fetch("http://192.168.73.143:8080/api/info?key=system");
    return r.json();
}

async function initSystemInfo() {
    const sys = await getSystemInfo();
    HOST = sys.hostname || "";
    CORES = Array.from({ length: sys.cpu_cores }, (_, i) => String(i));
    TOTAL_MEM_BYTES = sys.mem_total_bytes || 0;
}

async function getStored(){
    const url = `http://192.168.73.143:8080/api/stored`;
    try {
        const r = await fetch(url);
        if (!r.ok) throw new Error(`HTTP ${r.status}`);
        return await r.json(); // { metrics:[{name, labels:{...}}, ...] }
    } catch (err) {
        console.error('Failed to load stored metrics:', err);
    }
}


const BASE_DATAZOOM = [
    { type:'inside', start:70, end:100, minSpan:1 },
    { type:'slider', show:true, bottom:25, height:30, start:70, end:100, handleSize:12, showDetail:true }
];

function makeBaseOption(titleText=''){
    return {
        animation:true,
        toolbox:{ feature:{ dataZoom:{ yAxisIndex:'none' }, restore:{}, saveAsImage:{} } },
        title:{ text:titleText },
        tooltip:{ trigger:'axis' },
        grid:{ left:40, right:20, top:80, bottom:80 },
        xAxis:{ type:'category', data:[] },
        yAxis:{ type:'value', min:0, axisLabel:{ formatter:v=>v } },
        dataZoom: BASE_DATAZOOM,
        series:[{ name:titleText, type:'line', showSymbol:false, areaStyle:{ opacity:0.18 }, data:[] }]
    };
}

async function fetchTimeseries({ metric, labels=null, from=0, to=Date.now() }){
    const qs = new URLSearchParams({
        metric,
        from: String(from),
        to: String(to)
    });

    if (labels && typeof labels === 'object'){
        const kv = Object.entries(labels)
            .map(([k,v])=> `${k}:${encodeURIComponent(String(v))}`)
            .join(',');
        if (kv) qs.set('labels', kv);
    }

    const r = await fetch(`http://192.168.73.143:8080/api/query?${qs.toString()}`);
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return r.json();
}

function toSeriesPayload(info){
    const samples = Array.isArray(info.samples) ? info.samples.slice().sort((a,b)=>a[0]-b[0]) : [];
    const labels = samples.map(([ts]) => new Date(ts).toLocaleTimeString());
    const data   = samples.map(([_,v]) => v);
    const unit   = (info.unit || '').trim();
    const host   = info.labels?.host ? ` (${info.labels.host})` : '';
    const datasetLabel = `${info.metric}${host}${unit ? ` [${unit}]` : ''}`;
    return { labels, data, unit, datasetLabel };
}

function formatNumber(num, decimals = 2) {
    return Number(num.toFixed(decimals));
}

function bytesToMB(b) {
    return b / (1024 ** 2);
}

function bytesToGB(b) {
    return b / (1024 ** 3);
}

function convertMemoryValues(data, totalBytes) {
    // Choose best unit depending on the size
    if (totalBytes >= 1024 ** 3) {
        // Use GB
        return {
            data: data.map(v => formatNumber(bytesToGB(v), 2)),
            unit: "GB",
            yMax: formatNumber(bytesToGB(totalBytes), 2)
        };
    } else {
        // Use MB
        return {
            data: data.map(v => formatNumber(bytesToMB(v), 1)),
            unit: "MB",
            yMax: formatNumber(bytesToMB(totalBytes), 1)
        };
    }
}

function applyTimeseries(chart, payload) {
    if (!chart || !payload) return;

    let { labels, data, unit, datasetLabel } = payload;

    const isPercentage = unit === "%" || datasetLabel.includes("pct");
    const isMemory     = datasetLabel.startsWith("mem.");

    let yMax = null;

    if (isMemory && TOTAL_MEM_BYTES > 0) {
        const result = convertMemoryValues(data, TOTAL_MEM_BYTES);
        data = result.data;
        unit = result.unit;
        yMax = result.yMax;

        // Clean up dataset label, example:
        // mem.used_bytes → mem.used [GB]
        datasetLabel = datasetLabel.replace(/_bytes$/, "");
        datasetLabel += ` [${unit}]`;
    }

    if (isPercentage) {
        yMax = 100;
    }

    if (yMax == null) {
        const maxValue = Math.max(...data);
        yMax = maxValue * 1.1; // add 10% headroom
    }

    chart.setOption({
        title: {
            text: datasetLabel,
            left: "center"
        },
        tooltip: {
            trigger: "axis",
            formatter: params => {
                const v = params[0].value;
                const label = params[0].axisValue;

                if (isPercentage) {
                    return `${label}<br>${formatNumber(v, 2)}%`;
                } else {
                    return `${label}<br>${formatNumber(v, 2)} ${unit}`;
                }
            }
        },
        xAxis: {
            type: "category",
            data: labels
        },
        yAxis: {
            type: "value",
            min: 0,
            max: yMax,
            axisLabel: {
                formatter: v => {
                    if (isPercentage) return `${v}%`;
                    return `${v} ${unit}`;
                }
            }
        },
        series: [{
            type: "line",
            smooth: true,
            showSymbol: false,
            data
        }]
    }, false, false, ["series", "xAxis", "yAxis", "title"]);
}


/** Tiny registry to keep refresh functions in one place */
class ChartRegistry{
    constructor(){ this.items = []; }
    add({ chart, metric, labels=null, title=metric }){
        chart.setOption(makeBaseOption(title));
        const refresh = async () => {
            try{
                const info = await fetchTimeseries({ metric, labels });
                const payload = toSeriesPayload(info);
                applyTimeseries(chart, payload);
            }catch(e){ /* swallow to keep UI polling */ }
        };
        this.items.push({ refresh });
        return refresh; // return refresher if caller wants direct control
    }
    async refreshAll(){
        for (const it of this.items){ await it.refresh(); }
    }
}
const REG = new ChartRegistry();

function initCpuTotal(){
    const el = document.getElementById('chart-obj');
    myChart = echarts.init(el);
    // Register CPU total timeseries with the registry
    REG.add({ chart: myChart, metric:'cpu.total_pct', title:'cpu.total_pct' });
}

function initCoresChart(){
    const el = document.getElementById('chart-cores');
    if(!el) return;

    myCoresChart = echarts.init(el);
    myCoresChart.setOption({
        ...makeBaseOption('cpu.core_pct (per-core)'),
        legend:{ top:40 },
        yAxis:{ type:'value', min:0, max:100, axisLabel:{ formatter:v => `${v}%` } },
        series: [] // we'll inject multiple series dynamically
    });

    window.addEventListener('resize', ()=> myCoresChart && myCoresChart.resize());
}

function renderPerCore(labels, coreSeries){
    if(!myCoresChart) return;
    myCoresChart.setOption({
        xAxis:{ data: labels },
        series: coreSeries.map(s => ({
            name: `core ${s.core}`,
            type: 'line',
            showSymbol: false,
            areaStyle: { opacity: 0.18 },
            data: s.data
        })),
        legend:{ data: coreSeries.map(s => `core ${s.core}`) }
    }, false, false, ['series','xAxis','legend']);
}

async function loadAndRenderCores(){
    const info = await fetchTimeseries({
        metric: "cpu.core_pct",
        from: 0,
        to: Date.now()
    });

    if (!info.vector) return;  // safety

    const samples = info.samples || [];
    if (!samples.length) return;

    const labels = samples.map(([ts]) =>
        new Date(ts).toLocaleTimeString()
    );

    const coreCount = samples[0][1].length;

    const series = [];
    for (let i = 0; i < coreCount; i++) {
        series.push({
            core: i,
            data: samples.map(([_, vals]) => vals[i])
        });
    }

    renderPerCore(labels, series);
}


async function setupMemCharts(){
    if (MEM.ready) return;

    const stored = await getStored();
    if (!stored || !Array.isArray(stored.metrics)) return;

    MEM.metrics = stored.metrics
        .map(m => m.name)
        .filter(n => typeof n === 'string' && n.startsWith('mem'))
        .sort();

    const wrap = document.getElementById('mem-charts');
    if (!wrap) return;

    MEM.metrics.forEach(metric => {
        const id = `mem-chart-${metric.replace(/[^\w-]+/g,'_')}`;
        const box = document.createElement('div');
        box.className = 'mem-chart';
        box.id = id;
        wrap.appendChild(box);

        const ch = echarts.init(box);
        REG.add({ chart: ch, metric, title: metric });
        MEM.charts.set(metric, ch);
    });

    window.addEventListener('resize', () => MEM.charts.forEach(ch => ch.resize()));
    MEM.ready = true;
}

const fmt = {
    pct:v=>v==null?'':Number(v).toFixed(1),
    int:v=>v==null?'':Math.trunc(v),
    num1:v=>v==null?'':Number(v).toFixed(1),
    rss:v=>v==null?'':Number(v).toFixed(3),
    str:v=>v==null?'':String(v),
    hms:v=>{
        if(v==null) return '';
        const s=Math.floor(Number(v));
        const h=Math.floor(s/3600), m=Math.floor((s%3600)/60), sec=s%60;
        return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(sec).padStart(2,'0')}`;
    }
};

function loadProcesses() {
    fetch('http://192.168.73.143:8080/api/processes')
        .then(r => r.json())
        .then(rows => {
            const tbody = document.querySelector('#proc-table tbody');
            const frag  = document.createDocumentFragment();
            rows.forEach(p => {
                const tr = document.createElement('tr');
                tr.innerHTML = `
          <td class="num">${fmt.int(p.pid)}</td>
          <td title="${p.name}">${fmt.str(p.name)}</td>
          <td class="num">${fmt.pct(p.cpu_pct)}</td>
          <td class="num">${fmt.hms(p.cpu_time_s)}</td>
          <td class="num">${fmt.num1(p.idle_wakeups_per_s)}</td>
          <td class="num">${fmt.pct(p.mem_pct)}</td>
          <td class="num">${fmt.int(p.nice)}</td>
          <td class="num">${fmt.int(p.ppid)}</td>
          <td class="num">${fmt.int(p.priority)}</td>
          <td class="num">${fmt.rss(p.rss_mb)}</td>
          <td class="state">${fmt.str(p.state)}</td>
          <td class="num">${fmt.int(p.threads)}</td>
          <td class="user">${fmt.str(p.user)}</td>`;
                frag.appendChild(tr);
            });
            tbody.replaceChildren(frag);
        })
        .catch(() => {
            document.querySelector('#proc-table tbody')
                .innerHTML = `<tr><td colspan="13">Failed to load /api/processes</td></tr>`;
        });
}

(function enableColResize() {
    const table = document.getElementById('proc-table');
    if (!table) return;
    const cols  = table.querySelectorAll('colgroup col');
    const ths   = table.tHead?.rows?.[0]?.cells || [];

    [...ths].forEach((th, i) => {
        const grip = th.querySelector('.grip');
        if (!grip) return;
        let startX, startW;

        const onMove = (e) => {
            const dx = (e.clientX || e.touches?.[0]?.clientX) - startX;
            const w = Math.max(50, startW + dx);
            cols[i].style.width = w + 'px';
        };
        const onUp = () => {
            window.removeEventListener('mousemove', onMove);
            window.removeEventListener('mouseup', onUp);
            window.removeEventListener('touchmove', onMove);
            window.removeEventListener('touchend', onUp);
        };
        grip.addEventListener('mousedown', (e) => {
            startX = e.clientX; startW = th.getBoundingClientRect().width;
            window.addEventListener('mousemove', onMove);
            window.addEventListener('mouseup', onUp);
            e.preventDefault();
        });
        grip.addEventListener('touchstart', (e) => {
            startX = e.touches[0].clientX; startW = th.getBoundingClientRect().width;
            window.addEventListener('touchmove', onMove, {passive:false});
            window.addEventListener('touchend', onUp);
            e.preventDefault();
        }, {passive:false});
    });
})();

function activatePanel(panelId, tabBtn){
    document.querySelectorAll('.panel').forEach(p => p.classList.add('hidden'));
    document.getElementById(panelId)?.classList.remove('hidden');

    document.querySelectorAll('.tab-btn').forEach(b => b.setAttribute('aria-selected','false'));
    tabBtn.setAttribute('aria-selected','true');
}

function wireTabs(){
    const cpuBtn = document.getElementById('tab-cpu');
    const memBtn = document.getElementById('tab-mem');

    cpuBtn?.addEventListener('click', () => activatePanel('panel-cpu', cpuBtn));
    memBtn?.addEventListener('click', async () => {
        activatePanel('panel-mem', memBtn);
        // Lazy-init memory charts on first open
        await setupMemCharts();
        await REG.refreshAll(); // ensures mem charts render immediately
    });
}

function selectTimeFrame(seconds) {
    window.TIME_WINDOW_S = Number(seconds);
    // With registry-based refresh, we simply refresh everything;
    // backend can respect from/to if you wire it in.
    REG.refreshAll();
}
window.selectTimeFrame = selectTimeFrame;

document.addEventListener('DOMContentLoaded', async () => {
    await initSystemInfo();   // <-- load system info ONCE

    initCpuTotal();
    initCoresChart();
    wireTabs();

    loadProcesses();
    await REG.refreshAll();
    await loadAndRenderCores();

    setInterval(loadProcesses, 1000);
    setInterval(() => {
        REG.refreshAll();
        if (!document.getElementById('panel-mem').classList.contains('hidden')) {
            REG.refreshAll(); // only mem charts
        }
    }, 1000);

    setInterval(loadAndRenderCores, 1000);
});
