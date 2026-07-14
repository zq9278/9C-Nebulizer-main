#include "sensor_snapshot.h"

#include <string.h>

#include <zephyr/kernel.h>

static struct {
	struct k_mutex lock;
	sensor_snapshot_t snapshot;
} sensor_cache;

int sensor_snapshot_init(void)
{
	k_mutex_init(&sensor_cache.lock);
	memset(&sensor_cache.snapshot, 0, sizeof(sensor_cache.snapshot));
	return 0;
}

void sensor_snapshot_publish(const sensor_snapshot_t *snapshot)
{
	k_mutex_lock(&sensor_cache.lock, K_FOREVER);
	sensor_cache.snapshot = *snapshot;
	k_mutex_unlock(&sensor_cache.lock);
}

void sensor_snapshot_read(sensor_snapshot_t *snapshot)
{
	k_mutex_lock(&sensor_cache.lock, K_FOREVER);
	*snapshot = sensor_cache.snapshot;
	k_mutex_unlock(&sensor_cache.lock);
}
