#ifndef FACTORY_COMMON_H
#define FACTORY_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #define sleep_ms(ms) Sleep(ms)
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define sleep_ms(ms) usleep((ms) * 1000)
    #define closesocket(s) close(s)
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

#define MAX_WORKERS 8
#define DEFAULT_WORKERS 4
#define MAX_QUEUE_CAPACITY 128
#define MAX_COMPLETED_CAPACITY 256
#define MAX_LOG_ENTRIES 200

typedef enum {
    JOB_PENDING = 0,
    JOB_RUNNING = 1,
    JOB_COMPLETED = 2,
    JOB_CANCELLED = 3
} JobStatus;

typedef enum {
    WORKER_IDLE = 0,
    WORKER_WORKING = 1,
    WORKER_PAUSED = 2
} WorkerState;

typedef struct {
    int id;
    char name[64];
    double duration_sec;
    double progress;       /* 0.0 to 1.0 */
    double elapsed_sec;
    int assigned_worker_id;
    JobStatus status;
    char created_at[32];
    char completed_at[32];
} Job;

typedef struct {
    int id;
    pthread_t tid;
    unsigned long sys_thread_id;
    WorkerState state;
    int is_active;          /* active in current thread count setting */
    int has_job;
    Job current_job;
    int jobs_completed_count;
    double total_busy_time;
} Worker;

typedef struct {
    int id;
    char timestamp[32];
    int worker_id;          /* -1 for system/queue */
    char tag[24];           /* MUTEX, COND_VAR, WORKER, QUEUE, SYSTEM */
    char message[256];
} LogEntry;

typedef struct {
    int is_running;         /* 1 = started/processing, 0 = stopped/ready */
    int is_paused;          /* 1 = paused, 0 = active */
    int worker_count;       /* currently active workers (1-8) */
    int total_jobs_added;
    int total_jobs_completed;
    int next_job_id;
    int next_log_id;
    double sim_wall_clock_time;  /* actual elapsed time when simulation running */
    double sim_sequential_time;  /* sum of all completed job durations */
    int keep_alive;         /* controls server and worker lifecycle */
    
    /* Concurrency synchronization primitives */
    pthread_mutex_t sim_mutex;       /* protects all shared factory state */
    pthread_cond_t queue_cv;         /* signaled when jobs added or sim resumed */
    pthread_cond_t pause_cv;         /* signaled when pause state changes */
} SimulationState;

/* Global state singleton pointer declared across modules */
extern SimulationState g_sim;

/* Shared helper functions */
void get_current_time_str(char* buffer, size_t max_len);
double get_monotonic_time_sec(void);
void factory_log(int worker_id, const char* tag, const char* format, ...);

#endif /* FACTORY_COMMON_H */
