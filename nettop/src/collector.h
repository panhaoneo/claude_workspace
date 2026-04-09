#ifndef COLLECTOR_H
#define COLLECTOR_H

#include <pthread.h>

/* Collector thread entry point */
void *collector_thread(void *arg);

/* Signal the collector to stop */
void collector_stop(void);

#endif /* COLLECTOR_H */
