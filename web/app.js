// minisys Web Dashboard Controller

let prevRequests = 0;
let prevTime = Date.now();

// Format bytes
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

// Format duration
function formatUptime(seconds) {
    const d = Math.floor(seconds / (3600*24));
    const h = Math.floor(seconds % (3600*24) / 3600);
    const m = Math.floor(seconds % 3600 / 60);
    const s = Math.floor(seconds % 60);
    return `${d > 0 ? d + 'd ' : ''}${h}h ${m}m ${s}s`;
}

// Fetch Server Telemetry Stats
async function updateStats() {
    try {
        const res = await fetch('/api/stats');
        if (!res.ok) return;
        const data = await res.json();

        document.getElementById('stat-requests').innerText = data.total_requests.toLocaleString();
        document.getElementById('stat-keys').innerText = data.total_keys.toLocaleString();
        
        // Calculate hit ratio
        const totalGets = data.get_hits + data.get_misses;
        const hitRatio = totalGets > 0 ? ((data.get_hits / totalGets) * 100).toFixed(1) + '%' : '100%';
        document.getElementById('stat-hit-ratio').innerText = hitRatio;
        document.getElementById('stat-hits-misses').innerText = `${data.get_hits} Hits / ${data.get_misses} Misses`;

        // Calculate req/sec
        const now = Date.now();
        const dt = (now - prevTime) / 1000;
        if (dt > 0 && prevRequests > 0) {
            const rps = Math.max(0, Math.round((data.total_requests - prevRequests) / dt));
            document.getElementById('stat-req-rate').innerText = `${rps} req/sec`;
        }
        prevRequests = data.total_requests;
        prevTime = now;

        // IO stats
        const totalIo = data.bytes_sent + data.bytes_received;
        document.getElementById('stat-io').innerText = formatBytes(totalIo);
        document.getElementById('stat-io-breakdown').innerText = `Sent: ${formatBytes(data.bytes_sent)} | Recv: ${formatBytes(data.bytes_received)}`;
        document.getElementById('uptime-display').innerText = `Uptime: ${formatUptime(data.uptime_seconds)}`;

    } catch (e) {
        document.getElementById('status-text').innerText = 'Connection Lost';
        document.getElementById('status-text').style.color = 'var(--accent-rose)';
    }
}

// Load Key-Value Store Items
async function loadKeys() {
    try {
        const res = await fetch('/api/kv');
        if (!res.ok) return;
        const data = await res.json();

        const tbody = document.getElementById('kv-table-body');
        if (!data.keys || data.keys.length === 0) {
            tbody.innerHTML = `<tr><td colspan="3" class="empty-state">No keys found in database. Add one above!</td></tr>`;
            return;
        }

        tbody.innerHTML = '';
        for (const key of data.keys) {
            // Fetch individual key value
            const valRes = await fetch(`/api/kv?key=${encodeURIComponent(key)}`);
            let valStr = '(none)';
            if (valRes.ok) {
                const valData = await valRes.json();
                valStr = valData.value;
            }

            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>${escapeHtml(key)}</strong></td>
                <td>${escapeHtml(valStr)}</td>
                <td>
                    <button class="btn btn-danger" onclick="deleteKey('${escapeHtml(key)}')">Delete</button>
                </td>
            `;
            tbody.appendChild(tr);
        }
    } catch (e) {
        console.error('Failed to load keys', e);
    }
}

function escapeHtml(str) {
    return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

// Handle Form Submit (SET Key)
async function handleKvSubmit(e) {
    e.preventDefault();
    const key = document.getElementById('input-key').value.trim();
    const value = document.getElementById('input-val').value.trim();
    const ttl = parseInt(document.getElementById('input-ttl').value) || 0;

    if (!key || !value) return;

    try {
        const res = await fetch('/api/kv', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ key, value, ttl })
        });

        if (res.ok) {
            document.getElementById('input-key').value = '';
            document.getElementById('input-val').value = '';
            document.getElementById('input-ttl').value = '';
            loadKeys();
            updateStats();
        }
    } catch (err) {
        alert('Failed to set key: ' + err.message);
    }
}

// Delete Key
async function deleteKey(key) {
    if (!confirm(`Delete key "${key}"?`)) return;
    try {
        const res = await fetch(`/api/kv?key=${encodeURIComponent(key)}`, {
            method: 'DELETE'
        });
        if (res.ok) {
            loadKeys();
            updateStats();
        }
    } catch (e) {
        alert('Failed to delete key');
    }
}

// Interactive Browser Benchmark Runner
async function runBrowserBenchmark() {
    const btn = document.getElementById('bench-btn');
    const resultsBox = document.getElementById('bench-results');
    const concurrency = parseInt(document.getElementById('bench-concurrency').value);
    const totalRequests = parseInt(document.getElementById('bench-count').value);

    btn.disabled = true;
    btn.innerText = 'Testing...';
    resultsBox.style.display = 'flex';

    let completed = 0;
    let totalLatencyMs = 0;
    const startTime = performance.now();

    const updateProgress = () => {
        const pct = Math.round((completed / totalRequests) * 100);
        document.getElementById('bench-progress').style.width = `${pct}%`;
        document.getElementById('bench-completed').innerText = `${completed} / ${totalRequests}`;
    };

    const worker = async () => {
        while (completed < totalRequests) {
            completed++;
            const t0 = performance.now();
            try {
                await fetch('/api/stats');
            } catch (e) {}
            const t1 = performance.now();
            totalLatencyMs += (t1 - t0);
            if (completed % 10 === 0 || completed === totalRequests) {
                updateProgress();
            }
        }
    };

    const workers = [];
    for (let i = 0; i < concurrency; i++) {
        workers.push(worker());
    }

    await Promise.all(workers);

    const endTime = performance.now();
    const durationSec = (endTime - startTime) / 1000;
    const rps = Math.round(totalRequests / durationSec);
    const avgLat = (totalLatencyMs / totalRequests).toFixed(2);

    document.getElementById('bench-rps').innerText = `${rps.toLocaleString()} req/s`;
    document.getElementById('bench-avg-lat').innerText = `${avgLat} ms`;

    btn.disabled = false;
    btn.innerText = 'Start Benchmark';
    updateStats();
}

// Initial Load & Intervals
document.addEventListener('DOMContentLoaded', () => {
    updateStats();
    loadKeys();
    setInterval(updateStats, 2000);
});
