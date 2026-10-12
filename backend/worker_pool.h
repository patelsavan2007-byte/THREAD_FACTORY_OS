#ifndef FACTORY_WORKER_POOL_H
#define FACTORY_WORKER_POOL_H

#include "factory_common.h"
#include "queue.h"

extern Worker g_workers[MAX_WORKERS];

int worker_pool_init(int initial_count);
void worker_pool_shutdown(void);
int worker_pool_set_count(int count);
Worker* worker_pool_get(int id);
int worker_pool_get_active_count(void);

int sim_start(void);
int sim_pause(void);
int sim_resume(void);
int sim_reset(void);
int sim_add_job(const char* name, double duration_sec);
int sim_add_preset_batch(int count);

#endif /* FACTORY_WORKER_POOL_H */
