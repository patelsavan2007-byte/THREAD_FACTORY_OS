#include "factory_common.h"
#include "queue.h"
#include "worker_pool.h"
#include "http_server.h"
#include <signal.h>
#include <stdarg.h>

SimulationState g_sim;

void get_current_time_str(char* buffer, size_t max_len) {
    time_t raw;
    struct tm* info;
    time(&raw);
    info = localtime(&raw);
    if (info) {
        strftime(buffer, max_len, "%H:%M:%S", info);
    } else {
        snprintf(buffer, max_len, "00:00:00");
    }
}

void factory_log(int worker_id, const char* tag, const char* format, ...) {
    char msg[256];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);

    /* Also print to stdout for terminal visibility during demo */
    char time_str[32];
    get_current_time_str(time_str, sizeof(time_str));
    printf("[%s] [%-8s] %s\n", time_str, tag, msg);
    fflush(stdout);

    /* Save to thread-safe log history */
    logs_add(worker_id, tag, msg);
}

static void sig_handler(int sig) {
    (void)sig;
    printf("\n[Thread Factory] Received termination signal. Cleaning up...\n");
    g_sim.keep_alive = 0;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        int p = atoi(argv[1]);
        if (p > 1024 && p < 65535) port = p;
    }

    printf("====================================================\n");
    printf("   🏭 THREAD FACTORY - OS CONCURRENCY SIMULATION    \n");
    printf("      POSIX Threads (pthreads) Multithreading       \n");
    printf("====================================================\n\n");

    signal(SIGINT, sig_handler);
#ifndef _WIN32
    signal(SIGTERM, sig_handler);
#endif

    /* Initialize synchronization primitives */
    pthread_mutex_init(&g_sim.sim_mutex, NULL);
    pthread_cond_init(&g_sim.queue_cv, NULL);
    pthread_cond_init(&g_sim.pause_cv, NULL);

    g_sim.is_running = 0;
    g_sim.is_paused = 0;
    g_sim.worker_count = DEFAULT_WORKERS;
    g_sim.total_jobs_added = 0;
    g_sim.total_jobs_completed = 0;
    g_sim.next_job_id = 1;
    g_sim.next_log_id = 0;
    g_sim.sim_wall_clock_time = 0.0;
    g_sim.sim_sequential_time = 0.0;
    g_sim.keep_alive = 1;

    queue_init();
    completed_init();
    logs_init();

    /* Initialize worker thread pool */
    if (worker_pool_init(DEFAULT_WORKERS) != 0) {
        fprintf(stderr, "Failed to initialize worker thread pool.\n");
        return 1;
    }

    /* Prepopulate default introductory jobs for demonstration */
    sim_add_job("SMT Component Placement", 2.5);
    sim_add_job("Reflow Soldering", 3.0);
    sim_add_job("Optical Inspection", 1.8);
    sim_add_job("Firmware Flashing", 3.5);

    /* Start HTTP Server */
    if (http_server_start(port) != 0) {
        fprintf(stderr, "Failed to start HTTP server on port %d.\n", port);
        worker_pool_shutdown();
        return 1;
    }

    printf("[System] Ready! Open your browser at: http://localhost:%d\n", port);
    printf("[System] Press Ctrl+C in this console to stop.\n\n");

    /* Main loop: wait while keep_alive */
    while (g_sim.keep_alive) {
        sleep_ms(200);
    }

    printf("[System] Shutting down HTTP server...\n");
    http_server_stop();

    printf("[System] Shutting down worker threads...\n");
    worker_pool_shutdown();

    pthread_mutex_destroy(&g_sim.sim_mutex);
    pthread_cond_destroy(&g_sim.queue_cv);
    pthread_cond_destroy(&g_sim.pause_cv);

    printf("[System] Thread Factory terminated cleanly.\n");
    return 0;
}
