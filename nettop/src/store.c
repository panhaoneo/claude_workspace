#include <string.h>
#include "store.h"

nettop_store_t g_store;

void store_init(void)
{
    memset(&g_store, 0, sizeof(g_store));
    pthread_mutex_init(&g_store.lock, NULL);
    g_store.selected_nic = -1;
}

void store_destroy(void)
{
    pthread_mutex_destroy(&g_store.lock);
}
