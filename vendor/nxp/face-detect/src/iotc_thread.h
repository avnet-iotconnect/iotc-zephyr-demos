#ifndef _IOTC_THREAD_H
#define _IOTC_THREAD_H

#include <zephyr/kernel.h>

void iotc_thread(void *config, void *fifo, void *dummy3);

#endif