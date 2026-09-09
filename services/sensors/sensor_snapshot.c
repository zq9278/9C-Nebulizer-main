#include "sensor_snapshot.h"

#include <string.h>

#include <platform/runtime.h>

static struct {
	StaticSemaphore_t lock_storage;
	SemaphoreHandle_t lock;
	sensor_snapshot_t snapshot;
} sensor_cache;

int sensor_snapshot_init(void)
{
	sensor_cache.lock = xSemaphoreCreateMutexStatic(&sensor_cache.lock_storage);
	configASSERT(sensor_cache.lock != NULL);
	memset(&sensor_cache.snapshot, 0, sizeof(sensor_cache.snapshot));
	return 0;
}

void sensor_snapshot_publish(const sensor_snapshot_t *snapshot)
{
	xSemaphoreTake(sensor_cache.lock, portMAX_DELAY);
	sensor_cache.snapshot = *snapshot;
	xSemaphoreGive(sensor_cache.lock);
}

void sensor_snapshot_read(sensor_snapshot_t *snapshot)
{
	xSemaphoreTake(sensor_cache.lock, portMAX_DELAY);
	*snapshot = sensor_cache.snapshot;
	xSemaphoreGive(sensor_cache.lock);
}
