#include "worker_pool.h"
#include <stdint.h>

Worker g_workers[MAX_WORKERS];
static pthread_t g_wall_clock_tid;

/* Dedicated thread to track wall-clock simulation time when running */
static void* wall_clock_thread_func(void* arg) {
    (void)arg;
    while (g_sim.keep_alive) {
        sleep_ms(100);
        pthread_mutex_lock(&g_sim.sim_mutex);
        if (g_sim.is_running && !g_sim.is_paused) {
            /* Check if any workers are currently busy or queue has items */
            int busy_or_queued = (queue_count() > 0);
            if (!busy_or_queued) {
                for (int i = 0; i < g_sim.worker_count; i++) {
                    if (g_workers[i].state == WORKER_WORKING) {
                        busy_or_queued = 1;
                        break;
                    }
                }
            }
            if (busy_or_queued) {
                g_sim.sim_wall_clock_time += 0.10;
            }
        }
        pthread_mutex_unlock(&g_sim.sim_mutex);
    }
    return NULL;
}

/* Worker pthread routine */
static void* worker_thread_routine(void* arg) {
    Worker* worker = (Worker*)arg;
    
#ifdef _WIN32
    worker->sys_thread_id = (unsigned long)GetCurrentThreadId();
#else
    worker->sys_thread_id = (unsigned long)(uintptr_t)pthread_self();
#endif

    factory_log(worker->id, "THREAD", "[Worker #%d] POSIX Thread created (TID: 0x%lX). Entering ready loop.",
                worker->id + 1, worker->sys_thread_id);

    pthread_mutex_lock(&g_sim.sim_mutex);

    while (g_sim.keep_alive) {
        /* Check if worker is within current active pool limit */
        if (!worker->is_active) {
            worker->state = WORKER_IDLE;
            pthread_cond_wait(&g_sim.queue_cv, &g_sim.sim_mutex);
            continue;
        }

        /* If simulation is not started, or is paused, or no jobs in queue: wait */
        if (!g_sim.is_running || g_sim.is_paused || queue_count() == 0) {
            worker->state = (g_sim.is_paused && worker->has_job) ? WORKER_PAUSED : WORKER_IDLE;
            
            /* Atomic unlock and wait on condition variable */
            pthread_cond_wait(&g_sim.queue_cv, &g_sim.sim_mutex);
            continue;
        }

        /* Dequeue next job atomically under mutex */
        Job job;
        if (queue_pop(&job) == 0) {
            job.status = JOB_RUNNING;
            job.assigned_worker_id = worker->id;
            worker->current_job = job;
            worker->has_job = 1;
            worker->state = WORKER_WORKING;

            factory_log(worker->id, "MUTEX", "[Worker #%d] 🔒 Mutex acquired. Dequeued Job #%d '%s' (%.1fs). 🔓 Mutex released.",
                        worker->id + 1, job.id, job.name, job.duration_sec);

            /* Release mutex during processing so other threads execute concurrently */
            pthread_mutex_unlock(&g_sim.sim_mutex);

            /* Simulate task processing in 50ms slices to reflect live progress */
            double total_dur = job.duration_sec;
            double elapsed = 0.0;
            int cancelled = 0;

            while (elapsed < total_dur) {
                sleep_ms(50);

                pthread_mutex_lock(&g_sim.sim_mutex);
                if (!g_sim.keep_alive || !g_sim.is_running) {
                    cancelled = 1;
                    pthread_mutex_unlock(&g_sim.sim_mutex);
                    break;
                }

                /* If simulation paused, wait until resumed */
                while (g_sim.is_paused && g_sim.keep_alive && g_sim.is_running) {
                    worker->state = WORKER_PAUSED;
                    pthread_cond_wait(&g_sim.pause_cv, &g_sim.sim_mutex);
                }

                worker->state = WORKER_WORKING;
                elapsed += 0.05;
                if (elapsed > total_dur) {
                    elapsed = total_dur;
                }

                worker->current_job.elapsed_sec = elapsed;
                worker->current_job.progress = elapsed / total_dur;
                worker->total_busy_time += 0.05;
                pthread_mutex_unlock(&g_sim.sim_mutex);
            }

            /* Job completed or cancelled: re-lock mutex to update state */
            pthread_mutex_lock(&g_sim.sim_mutex);
            if (!cancelled && g_sim.keep_alive) {
                worker->current_job.status = JOB_COMPLETED;
                worker->current_job.progress = 1.0;
                worker->current_job.elapsed_sec = total_dur;
                get_current_time_str(worker->current_job.completed_at, sizeof(worker->current_job.completed_at));

                completed_add(&worker->current_job);
                worker->jobs_completed_count++;
                g_sim.total_jobs_completed++;
                g_sim.sim_sequential_time += total_dur;

                factory_log(worker->id, "WORKER", "[Worker #%d] ✅ Finished Job #%d '%s' in %.1fs. Idle now.",
                            worker->id + 1, worker->current_job.id, worker->current_job.name, total_dur);
            }

            worker->has_job = 0;
            worker->state = WORKER_IDLE;
            memset(&worker->current_job, 0, sizeof(Job));

            /* If more jobs are pending, signal condition variable for other workers */
            if (queue_count() > 0) {
                pthread_cond_signal(&g_sim.queue_cv);
            }
        }
    }

    pthread_mutex_unlock(&g_sim.sim_mutex);
    return NULL;
}

int worker_pool_init(int initial_count) {
    if (initial_count < 1) initial_count = 1;
    if (initial_count > MAX_WORKERS) initial_count = MAX_WORKERS;

    g_sim.worker_count = initial_count;

    for (int i = 0; i < MAX_WORKERS; i++) {
        g_workers[i].id = i;
        g_workers[i].state = WORKER_IDLE;
        g_workers[i].is_active = (i < initial_count);
        g_workers[i].has_job = 0;
        g_workers[i].jobs_completed_count = 0;
        g_workers[i].total_busy_time = 0.0;
        g_workers[i].sys_thread_id = 0;
        memset(&g_workers[i].current_job, 0, sizeof(Job));

        int rc = pthread_create(&g_workers[i].tid, NULL, worker_thread_routine, &g_workers[i]);
        if (rc != 0) {
            fprintf(stderr, "Failed to create worker thread %d\n", i);
            return -1;
        }
    }

    /* Create background wall-clock simulation monitor thread */
    pthread_create(&g_wall_clock_tid, NULL, wall_clock_thread_func, NULL);

    factory_log(-1, "SYSTEM", "Worker thread pool initialized with %d active threads (POSIX pthreads).", initial_count);
    return 0;
}

void worker_pool_shutdown(void) {
    pthread_mutex_lock(&g_sim.sim_mutex);
    g_sim.keep_alive = 0;
    pthread_cond_broadcast(&g_sim.queue_cv);
    pthread_cond_broadcast(&g_sim.pause_cv);
    pthread_mutex_unlock(&g_sim.sim_mutex);

    for (int i = 0; i < MAX_WORKERS; i++) {
        pthread_join(g_workers[i].tid, NULL);
    }
    pthread_join(g_wall_clock_tid, NULL);
}

int worker_pool_set_count(int count) {
    if (count < 1 || count > MAX_WORKERS) {
        return -1;
    }

    pthread_mutex_lock(&g_sim.sim_mutex);
    int old_count = g_sim.worker_count;
    g_sim.worker_count = count;

    for (int i = 0; i < MAX_WORKERS; i++) {
        g_workers[i].is_active = (i < count);
    }

    factory_log(-1, "SYSTEM", "Worker thread count adjusted from %d to %d.", old_count, count);
    pthread_cond_broadcast(&g_sim.queue_cv);
    pthread_mutex_unlock(&g_sim.sim_mutex);
    return 0;
}

Worker* worker_pool_get(int id) {
    if (id < 0 || id >= MAX_WORKERS) {
        return NULL;
    }
    return &g_workers[id];
}

int worker_pool_get_active_count(void) {
    return g_sim.worker_count;
}

int sim_start(void) {
    pthread_mutex_lock(&g_sim.sim_mutex);
    if (!g_sim.is_running) {
        g_sim.is_running = 1;
        g_sim.is_paused = 0;
        factory_log(-1, "SYSTEM", "▶ Simulation STARTED. Waking worker threads.");
        pthread_cond_broadcast(&g_sim.queue_cv);
    } else if (g_sim.is_paused) {
        g_sim.is_paused = 0;
        factory_log(-1, "SYSTEM", "▶ Simulation RESUMED from pause.");
        pthread_cond_broadcast(&g_sim.queue_cv);
        pthread_cond_broadcast(&g_sim.pause_cv);
    }
    pthread_mutex_unlock(&g_sim.sim_mutex);
    return 0;
}

int sim_pause(void) {
    pthread_mutex_lock(&g_sim.sim_mutex);
    if (g_sim.is_running && !g_sim.is_paused) {
        g_sim.is_paused = 1;
        factory_log(-1, "SYSTEM", "⏸ Simulation PAUSED. Workers will pause current slices.");
        pthread_cond_broadcast(&g_sim.pause_cv);
    }
    pthread_mutex_unlock(&g_sim.sim_mutex);
    return 0;
}

int sim_resume(void) {
    pthread_mutex_lock(&g_sim.sim_mutex);
    if (g_sim.is_running && g_sim.is_paused) {
        g_sim.is_paused = 0;
        factory_log(-1, "SYSTEM", "⏯ Simulation RESUMED. Notifying worker threads.");
        pthread_cond_broadcast(&g_sim.queue_cv);
        pthread_cond_broadcast(&g_sim.pause_cv);
    }
    pthread_mutex_unlock(&g_sim.sim_mutex);
    return 0;
}

int sim_reset(void) {
    pthread_mutex_lock(&g_sim.sim_mutex);
    g_sim.is_running = 0;
    g_sim.is_paused = 0;
    g_sim.sim_wall_clock_time = 0.0;
    g_sim.sim_sequential_time = 0.0;
    g_sim.total_jobs_added = 0;
    g_sim.total_jobs_completed = 0;
    g_sim.next_job_id = 1;

    queue_clear();
    completed_clear();
    logs_clear();

    for (int i = 0; i < MAX_WORKERS; i++) {
        g_workers[i].state = WORKER_IDLE;
        g_workers[i].has_job = 0;
        g_workers[i].jobs_completed_count = 0;
        g_workers[i].total_busy_time = 0.0;
        memset(&g_workers[i].current_job, 0, sizeof(Job));
    }

    factory_log(-1, "SYSTEM", "🔄 Simulation RESET. Queue cleared and metrics re-initialized.");
    pthread_cond_broadcast(&g_sim.queue_cv);
    pthread_cond_broadcast(&g_sim.pause_cv);
    pthread_mutex_unlock(&g_sim.sim_mutex);
    return 0;
}

int sim_add_job(const char* name, double duration_sec) {
    if (duration_sec <= 0.0) duration_sec = 1.0;
    if (duration_sec > 60.0) duration_sec = 60.0;

    pthread_mutex_lock(&g_sim.sim_mutex);

    Job job;
    memset(&job, 0, sizeof(Job));
    job.id = g_sim.next_job_id++;
    strncpy(job.name, (name && strlen(name) > 0) ? name : "Factory Job", sizeof(job.name) - 1);
    job.duration_sec = duration_sec;
    job.progress = 0.0;
    job.elapsed_sec = 0.0;
    job.assigned_worker_id = -1;
    job.status = JOB_PENDING;
    get_current_time_str(job.created_at, sizeof(job.created_at));

    int res = queue_push(&job);
    if (res == 0) {
        g_sim.total_jobs_added++;
        factory_log(-1, "QUEUE", "📦 Added Job #%d '%s' (%.1fs) to queue. Signaling condition variable.",
                    job.id, job.name, job.duration_sec);
        /* Signal one waiting worker */
        pthread_cond_signal(&g_sim.queue_cv);
    } else {
        factory_log(-1, "QUEUE", "⚠️ Queue is full! Could not add Job '%s'.", job.name);
    }

    pthread_mutex_unlock(&g_sim.sim_mutex);
    return (res == 0) ? job.id : -1;
}

int sim_add_preset_batch(int count) {
    static const struct {
        const char* name;
        double duration;
    } presets[] = {
        { "PCB SMT Assembly", 2.5 },
        { "Reflow Soldering", 3.0 },
        { "Optical Inspection", 1.8 },
        { "Firmware Flashing", 3.5 },
        { "RF Antenna Test", 2.0 },
        { "Thermal Stress Check", 4.0 },
        { "Laser Labeling", 1.5 },
        { "Final Packaging", 2.2 }
    };
    int num_presets = (int)(sizeof(presets) / sizeof(presets[0]));

    int added = 0;
    for (int i = 0; i < count; i++) {
        int idx = i % num_presets;
        if (sim_add_job(presets[idx].name, presets[idx].duration) > 0) {
            added++;
        }
    }
    return added;
}
