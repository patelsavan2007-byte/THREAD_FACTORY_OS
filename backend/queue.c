#include "queue.h"

JobQueue g_queue;
CompletedJobs g_completed;
LogHistory g_logs;

void queue_init(void) {
    g_queue.head = 0;
    g_queue.tail = 0;
    g_queue.count = 0;
    memset(g_queue.items, 0, sizeof(g_queue.items));
}

int queue_push(const Job* job) {
    if (g_queue.count >= MAX_QUEUE_CAPACITY) {
        return -1; /* Queue full */
    }
    g_queue.items[g_queue.tail] = *job;
    g_queue.tail = (g_queue.tail + 1) % MAX_QUEUE_CAPACITY;
    g_queue.count++;
    return 0;
}

int queue_pop(Job* out_job) {
    if (g_queue.count == 0) {
        return -1; /* Queue empty */
    }
    *out_job = g_queue.items[g_queue.head];
    g_queue.head = (g_queue.head + 1) % MAX_QUEUE_CAPACITY;
    g_queue.count--;
    return 0;
}

void queue_clear(void) {
    g_queue.head = 0;
    g_queue.tail = 0;
    g_queue.count = 0;
}

int queue_count(void) {
    return g_queue.count;
}

Job* queue_peek_at(int index) {
    if (index < 0 || index >= g_queue.count) {
        return NULL;
    }
    int idx = (g_queue.head + index) % MAX_QUEUE_CAPACITY;
    return &g_queue.items[idx];
}

void completed_init(void) {
    g_completed.count = 0;
    memset(g_completed.items, 0, sizeof(g_completed.items));
}

void completed_add(const Job* job) {
    if (g_completed.count >= MAX_COMPLETED_CAPACITY) {
        /* Shift left by 1 to make room if max capacity reached */
        memmove(&g_completed.items[0], &g_completed.items[1], sizeof(Job) * (MAX_COMPLETED_CAPACITY - 1));
        g_completed.count = MAX_COMPLETED_CAPACITY - 1;
    }
    g_completed.items[g_completed.count++] = *job;
}

void completed_clear(void) {
    g_completed.count = 0;
}

int completed_count(void) {
    return g_completed.count;
}

Job* completed_peek_at(int index) {
    if (index < 0 || index >= g_completed.count) {
        return NULL;
    }
    return &g_completed.items[index];
}

void logs_init(void) {
    g_logs.head = 0;
    g_logs.count = 0;
    g_logs.total_logged = 0;
    memset(g_logs.items, 0, sizeof(g_logs.items));
}

void logs_add(int worker_id, const char* tag, const char* message) {
    int insert_idx;
    if (g_logs.count < MAX_LOG_ENTRIES) {
        insert_idx = (g_logs.head + g_logs.count) % MAX_LOG_ENTRIES;
        g_logs.count++;
    } else {
        insert_idx = g_logs.head;
        g_logs.head = (g_logs.head + 1) % MAX_LOG_ENTRIES;
    }

    LogEntry* entry = &g_logs.items[insert_idx];
    entry->id = ++g_logs.total_logged;
    entry->worker_id = worker_id;
    strncpy(entry->tag, tag, sizeof(entry->tag) - 1);
    entry->tag[sizeof(entry->tag) - 1] = '\0';
    strncpy(entry->message, message, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';
    get_current_time_str(entry->timestamp, sizeof(entry->timestamp));
}

void logs_clear(void) {
    g_logs.head = 0;
    g_logs.count = 0;
    g_logs.total_logged = 0;
}

int logs_count(void) {
    return g_logs.count;
}

LogEntry* logs_peek_at(int index) {
    if (index < 0 || index >= g_logs.count) {
        return NULL;
    }
    int idx = (g_logs.head + index) % MAX_LOG_ENTRIES;
    return &g_logs.items[idx];
}
