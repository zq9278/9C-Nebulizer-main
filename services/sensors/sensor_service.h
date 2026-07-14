#ifndef SERVICES_SENSORS_SENSOR_SERVICE_H_
#define SERVICES_SENSORS_SENSOR_SERVICE_H_

#include <nebulizer/app_types.h>

int sensor_service_init(void);
int sensor_service_sample(sensor_snapshot_t *snapshot);

#endif /* SERVICES_SENSORS_SENSOR_SERVICE_H_ */
