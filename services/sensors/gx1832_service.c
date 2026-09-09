#include <platform/util.h>
#include "gx1832_service.h"

#include <errno.h>

int gx1832_service_init(void)
{
	return 0;
}

int gx1832_service_query(bool *active)
{
	ARG_UNUSED(active);
	return -ENOTSUP;
}
