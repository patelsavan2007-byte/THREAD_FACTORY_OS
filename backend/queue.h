#ifndef FACTORY_QUEUE_H
#define FACTORY_QUEUE_H

#include "factory_common.h"

/* Job FIFO Queue */
typedef struct {
    Job items[MAX_QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
} JobQueue;

/* Completed Jobs History */
typedef struct {
    Job items[MAX_COMPLETED_CAPACITY];
    int count;
} CompletedJobs;

/* Ring Buffer of Log Entries */
typedef struct {
    LogEntry items[MAX_LOG_ENTRIES];
    int head;
    int count;
    int total_logged;
} LogHistory;

extern JobQueue g_queue;
extern CompletedJobs g_completed;
extern LogHistory g_logs;

void queue_init(void);
int queue_push(const Job* job);
int queue_pop(Job* out_job);
void queue_clear(void);
int queue_count(void);
Job* queue_peek_at(int index);

void completed_init(void);
void completed_add(const Job* job);
void completed_clear(void);
int completed_count(void);
Job* completed_peek_at(int index);

void logs_init(void);
void logs_add(int worker_id, const char* tag, const char* message);
void logs_clear(void);
int logs_count(void);
LogEntry* logs_peek_at(int index);

#endif /* FACTORY_QUEUE_H */
