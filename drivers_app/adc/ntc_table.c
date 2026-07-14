#include "ntc_table.h"

#include <zephyr/sys/util.h>

static const struct ntc_table_entry ntc_table_default[] = {
	{ -100, 58245 }, { -50, 44117 }, { 0, 33621 },   { 50, 25866 },
	{ 100, 20098 },  { 150, 15715 }, { 200, 12348 }, { 250, 10000 },
	{ 300, 8150 },   { 350, 6686 },  { 400, 5513 },  { 450, 4560 },
	{ 500, 3788 },   { 550, 3156 },  { 600, 2637 },  { 650, 2209 },
	{ 700, 1854 },   { 750, 1560 },  { 800, 1314 },
};

const struct ntc_table_entry *ntc_table_get_default(size_t *count)
{
	if (count != NULL) {
		*count = ARRAY_SIZE(ntc_table_default);
	}

	return ntc_table_default;
}
