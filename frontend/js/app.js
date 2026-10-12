/**
 * Thread Factory — OS Concurrency Simulator
 * Frontend Client Application
 * Communicates with the C pthreads backend REST API.
 */

(() => {
    // API Endpoint config (relative for same-origin or localhost fallback)
    const API_BASE = window.location.origin.includes('http') ? window.location.origin : 'http://localhost:8080';
    const POLL_INTERVAL_MS = 150; // Smooth 150ms state polling

    // DOM Elements
    const elements = {
        backendStatus: document.getElementById('backendStatus'),
        simStateBadge: document.getElementById('simStateBadge'),
        btnStart: document.getElementById('btnStart'),
        btnPause: document.getElementById('btnPause'),
        btnResume: document.getElementById('btnResume'),
        btnReset: document.getElementById('btnReset'),
        workerCountGroup: document.getElementById('workerCountGroup'),
        
        // KPIs
        kpiTotalJobs: document.getElementById('kpiTotalJobs'),
        kpiPendingJobs: document.getElementById('kpiPendingJobs'),
        kpiActiveWorkers: document.getElementById('kpiActiveWorkers'),
        kpiTotalWorkers: document.getElementById('kpiTotalWorkers'),
        kpiCompletedJobs: document.getElementById('kpiCompletedJobs'),
        kpiSpeedup: document.getElementById('kpiSpeedup'),
        kpiSpeedupDetails: document.getElementById('kpiSpeedupDetails'),

        // Dispatch
        jobForm: document.getElementById('jobForm'),
        jobNameInput: document.getElementById('jobNameInput'),
        jobDurationInput: document.getElementById('jobDurationInput'),
        btnAddBatch4: document.getElementById('btnAddBatch4'),
        btnAddBatch8: document.getElementById('btnAddBatch8'),

        // Floor
        activePoolLabel: document.getElementById('activePoolLabel'),
        workerCardsContainer: document.getElementById('workerCardsContainer'),

        // Queue & History
        badgeQueueCount: document.getElementById('badgeQueueCount'),
        badgeCompletedCount: document.getElementById('badgeCompletedCount'),
        queueTableBody: document.getElementById('queueTableBody'),
        completedTableBody: document.getElementById('completedTableBody'),

        // Logs
        terminalWindow: document.getElementById('terminalWindow'),
        logStream: document.getElementById('logStream'),
        btnClearLogs: document.getElementById('btnClearLogs'),
        chkAutoScroll: document.getElementById('chkAutoScroll'),

        // Modal
        btnOpenDocs: document.getElementById('btnOpenDocs'),
        btnCloseModal: document.getElementById('btnCloseModal'),
        btnUnderstand: document.getElementById('btnUnderstand'),
        conceptsModal: document.getElementById('conceptsModal')
    };

    let currentFilter = 'ALL';
    let isConnected = false;
    let localLogsCleared = false;
    let lastRenderedLogId = 0;

    // --- API Communication Helper ---
    async function apiRequest(endpoint, method = 'GET', body = null) {
        try {
            const options = {
                method,
                headers: { 'Content-Type': 'application/json' }
            };
            if (body) {
                options.body = JSON.stringify(body);
            }
            const res = await fetch(`${API_BASE}${endpoint}`, options);
            if (!res.ok) throw new Error(`HTTP error ${res.status}`);
            return await res.json();
        } catch (err) {
            return null;
        }
    }

    // --- State Polling Routine ---
    async function fetchState() {
        const state = await apiRequest('/api/status');
        if (!state) {
            setConnected(false);
            return;
        }

        setConnected(true);
        updateDashboard(state);
        updateControls(state);
        updateWorkers(state.workers || []);
        updateQueue(state.queue || []);
        updateCompleted(state.completed || []);
        updateLogs(state.logs || []);
    }

    function setConnected(connected) {
        if (isConnected === connected) return;
        isConnected = connected;

        const dot = elements.backendStatus.querySelector('.status-dot');
        const label = elements.backendStatus.querySelector('.status-label');

        if (connected) {
            dot.className = 'status-dot connected';
            label.textContent = 'C Backend Online';
        } else {
            dot.className = 'status-dot disconnected';
            label.textContent = 'Backend Offline';
            elements.simStateBadge.textContent = 'OFFLINE';
            elements.simStateBadge.className = 'status-badge state-badge';
        }
    }

    // --- Update Dashboard KPIs ---
    function updateDashboard(state) {
        elements.kpiTotalJobs.textContent = state.total_jobs_added;
        elements.kpiPendingJobs.textContent = state.pending_jobs_count;
        elements.kpiActiveWorkers.textContent = state.active_workers_count;
        elements.kpiTotalWorkers.textContent = state.worker_count;
        elements.kpiCompletedJobs.textContent = state.total_jobs_completed;

        elements.badgeQueueCount.textContent = state.pending_jobs_count;
        elements.badgeCompletedCount.textContent = state.total_jobs_completed;
        elements.activePoolLabel.textContent = `${state.worker_count} Active Worker Threads`;

        // Concurrency Speedup Metric
        const speedup = (state.speedup_ratio || 1.0).toFixed(2);
        elements.kpiSpeedup.textContent = `${speedup}x`;
        elements.kpiSpeedupDetails.textContent = `Seq: ${state.sim_sequential_time.toFixed(1)}s | Par: ${state.sim_wall_clock_time.toFixed(1)}s`;

        // State Badge
        const badge = elements.simStateBadge;
        const textSpan = badge.querySelector('.sim-state-text') || badge;
        if (!state.is_running) {
            textSpan.textContent = 'READY';
            badge.className = 'status-badge state-badge';
        } else if (state.is_paused) {
            textSpan.textContent = 'PAUSED';
            badge.className = 'status-badge state-badge paused';
        } else {
            textSpan.textContent = 'RUNNING';
            badge.className = 'status-badge state-badge running';
        }
    }

    // --- Update Controls State ---
    function updateControls(state) {
        if (!state.is_running) {
            elements.btnStart.disabled = false;
            elements.btnPause.disabled = true;
            elements.btnResume.disabled = true;
        } else if (state.is_paused) {
            elements.btnStart.disabled = true;
            elements.btnPause.disabled = true;
            elements.btnResume.disabled = false;
        } else {
            elements.btnStart.disabled = true;
            elements.btnPause.disabled = false;
            elements.btnResume.disabled = true;
        }

        // Worker count button active state
        document.querySelectorAll('#workerCountGroup .btn-toggle').forEach(btn => {
            const count = parseInt(btn.dataset.count, 10);
            if (count === state.worker_count) {
                btn.classList.add('active');
            } else {
                btn.classList.remove('active');
            }
        });
    }

    // --- Update Worker Cards ---
    function updateWorkers(workers) {
        const container = elements.workerCardsContainer;
        
        // Build cards markup
        let html = '';
        workers.forEach(w => {
            const isWorking = w.state === 'WORKING';
            const isPaused = w.state === 'PAUSED';
            const cardClass = isWorking ? 'worker-card working' : (isPaused ? 'worker-card paused' : 'worker-card');
            const badgeClass = isWorking ? 'working' : (isPaused ? 'paused' : 'idle');

            let jobDetailsHtml = '';
            let progressWidth = 0;
            let progressLabel = '0% (0.0s / 0.0s)';

            if (w.has_job && w.current_job) {
                const j = w.current_job;
                const pct = Math.min(100, Math.round(j.progress * 100));
                progressWidth = pct;
                progressLabel = `${pct}% (${j.elapsed_sec.toFixed(1)}s / ${j.duration_sec.toFixed(1)}s)`;

                jobDetailsHtml = `
                    <div class="job-label">Current Job</div>
                    <div class="job-details-row">
                        <span class="job-name-display" title="${j.name}">#${j.id} ${j.name}</span>
                        <span class="job-time-display">${j.duration_sec.toFixed(1)}s</span>
                    </div>
                `;
            } else {
                jobDetailsHtml = `
                    <div class="job-label">Status</div>
                    <div class="job-idle-placeholder">Waiting on pthread condition variable...</div>
                `;
            }

            html += `
                <div class="${cardClass}" id="workerCard-${w.id}">
                    <div class="worker-card-header">
                        <div class="worker-title-group">
                            <span class="worker-name">Worker #${w.id}</span>
                            <span class="worker-tid" title="System Thread ID">TID: ${w.sys_thread_id}</span>
                        </div>
                        <span class="worker-status-badge ${badgeClass}">
                            <span class="worker-pulse-dot"></span>
                            <span>${w.state}</span>
                        </span>
                    </div>

                    <div class="worker-job-box">
                        ${jobDetailsHtml}
                    </div>

                    <div class="progress-container">
                        <div class="progress-track">
                            <div class="progress-fill ${isPaused ? 'paused' : ''}" style="width: ${progressWidth}%;"></div>
                        </div>
                        <div class="progress-label-row">
                            <span>Processing</span>
                            <span>${progressLabel}</span>
                        </div>
                    </div>

                    <div class="worker-footer">
                        <span>Done: <strong>${w.jobs_completed_count}</strong></span>
                        <span>Busy Time: <strong>${(w.total_busy_time || 0).toFixed(1)}s</strong></span>
                    </div>
                </div>
            `;
        });

        container.innerHTML = html;
    }

    // --- Update Job Queue Table ---
    function updateQueue(queue) {
        const tbody = elements.queueTableBody;
        if (!queue || queue.length === 0) {
            tbody.innerHTML = `<tr><td colspan="4" class="empty-cell">Queue is empty. Add a job above!</td></tr>`;
            return;
        }

        let html = '';
        queue.forEach(j => {
            html += `
                <tr>
                    <td><strong>#${j.id}</strong></td>
                    <td>${escapeHtml(j.name)}</td>
                    <td><code>${j.duration_sec.toFixed(1)}s</code></td>
                    <td><span class="status-tag pending">Waiting in Queue</span></td>
                </tr>
            `;
        });
        tbody.innerHTML = html;
    }

    // --- Update Completed Jobs Table ---
    function updateCompleted(completed) {
        const tbody = elements.completedTableBody;
        if (!completed || completed.length === 0) {
            tbody.innerHTML = `<tr><td colspan="5" class="empty-cell">No jobs completed yet.</td></tr>`;
            return;
        }

        let html = '';
        completed.forEach(j => {
            html += `
                <tr>
                    <td><strong>#${j.id}</strong></td>
                    <td>${escapeHtml(j.name)}</td>
                    <td><code>${j.duration_sec.toFixed(1)}s</code></td>
                    <td>Worker #${j.worker_id}</td>
                    <td>${j.completed_at || '-'}</td>
                </tr>
            `;
        });
        tbody.innerHTML = html;
    }

    // --- Update Execution Logs Terminal ---
    function updateLogs(logs) {
        if (localLogsCleared) return;
        if (!logs || logs.length === 0) {
            elements.logStream.innerHTML = '<div class="log-line"><span class="log-msg">Logs initialized. Waiting for thread events...</span></div>';
            return;
        }

        // Filter logs based on chip selection
        const filtered = logs.filter(l => {
            if (currentFilter === 'ALL') return true;
            return l.tag === currentFilter;
        });

        // Display in chronological order (oldest to newest)
        const chron = [...filtered].reverse();

        let html = '';
        chron.forEach(l => {
            html += `
                <div class="log-line">
                    <span class="log-time">[${l.timestamp}]</span>
                    <span class="log-tag ${l.tag}">[${l.tag}]</span>
                    <span class="log-msg">${escapeHtml(l.message)}</span>
                </div>
            `;
        });

        elements.logStream.innerHTML = html;

        if (elements.chkAutoScroll.checked) {
            elements.terminalWindow.scrollTop = elements.terminalWindow.scrollHeight;
        }
    }

    function escapeHtml(str) {
        if (!str) return '';
        return str
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
    }

    // --- Simulation Control Handlers ---
    elements.btnStart.addEventListener('click', async () => {
        await apiRequest('/api/control', 'POST', { action: 'start' });
        fetchState();
    });

    elements.btnPause.addEventListener('click', async () => {
        await apiRequest('/api/control', 'POST', { action: 'pause' });
        fetchState();
    });

    elements.btnResume.addEventListener('click', async () => {
        await apiRequest('/api/control', 'POST', { action: 'resume' });
        fetchState();
    });

    elements.btnReset.addEventListener('click', async () => {
        localLogsCleared = false;
        await apiRequest('/api/control', 'POST', { action: 'reset' });
        fetchState();
    });

    // Worker Threads count toggle
    elements.workerCountGroup.addEventListener('click', async (e) => {
        const btn = e.target.closest('.btn-toggle');
        if (!btn) return;
        const count = parseInt(btn.dataset.count, 10);
        await apiRequest('/api/control', 'POST', { action: 'set_workers', count });
        fetchState();
    });

    // Dispatch form
    elements.jobForm.addEventListener('submit', async (e) => {
        e.preventDefault();
        const name = elements.jobNameInput.value.trim() || 'Factory Task';
        const duration = parseFloat(elements.jobDurationInput.value) || 2.0;

        await apiRequest('/api/jobs', 'POST', { name, duration });
        elements.jobNameInput.value = '';
        fetchState();
    });

    // Preset duration buttons
    document.querySelectorAll('.chip-duration').forEach(chip => {
        chip.addEventListener('click', () => {
            elements.jobDurationInput.value = chip.dataset.dur;
        });
    });

    // Batch Add buttons
    elements.btnAddBatch4.addEventListener('click', async () => {
        await apiRequest('/api/batch', 'POST', { count: 4 });
        fetchState();
    });

    elements.btnAddBatch8.addEventListener('click', async () => {
        await apiRequest('/api/batch', 'POST', { count: 8 });
        fetchState();
    });

    // Tab Switching (Job Queue vs Completed)
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));

            btn.classList.add('active');
            const targetId = btn.dataset.tab;
            const targetContent = document.getElementById(targetId);
            if (targetContent) targetContent.classList.add('active');
        });
    });

    // Log Filter Chips
    document.querySelectorAll('.filter-chip').forEach(chip => {
        chip.addEventListener('click', () => {
            document.querySelectorAll('.filter-chip').forEach(c => c.classList.remove('active'));
            chip.classList.add('active');
            currentFilter = chip.dataset.filter;
            localLogsCleared = false;
            fetchState();
        });
    });

    elements.btnClearLogs.addEventListener('click', () => {
        localLogsCleared = true;
        elements.logStream.innerHTML = '<div class="log-line"><span class="log-msg">Logs cleared by user. (Will resume on new events)</span></div>';
        setTimeout(() => { localLogsCleared = false; }, 3000);
    });

    // OS Concepts Modal Handlers
    elements.btnOpenDocs.addEventListener('click', () => {
        elements.conceptsModal.style.display = 'flex';
    });
    elements.btnCloseModal.addEventListener('click', () => {
        elements.conceptsModal.style.display = 'none';
    });
    elements.btnUnderstand.addEventListener('click', () => {
        elements.conceptsModal.style.display = 'none';
    });
    elements.conceptsModal.addEventListener('click', (e) => {
        if (e.target === elements.conceptsModal) {
            elements.conceptsModal.style.display = 'none';
        }
    });

    // Initial load and ongoing polling
    fetchState();
    setInterval(fetchState, POLL_INTERVAL_MS);
})();
