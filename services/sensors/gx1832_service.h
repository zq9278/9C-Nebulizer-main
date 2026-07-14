#ifndef SERVICES_SENSORS_GX1832_SERVICE_H_
#define SERVICES_SENSORS_GX1832_SERVICE_H_

#include <stdbool.h>

int gx1832_service_init(void);
int gx1832_service_query(bool *active);

#endif /* SERVICES_SENSORS_GX1832_SERVICE_H_ */
