#ifndef SERVICES_SENSORS_SENSOR_SNAPSHOT_H_
#define SERVICES_SENSORS_SENSOR_SNAPSHOT_H_

#include <nebulizer/app_types.h>

int sensor_snapshot_init(void);
void sensor_snapshot_publish(const sensor_snapshot_t *snapshot);
void sensor_snapshot_read(sensor_snapshot_t *snapshot);

#endif /* SERVICES_SENSORS_SENSOR_SNAPSHOT_H_ */
