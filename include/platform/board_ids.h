#ifndef PLATFORM_BOARD_IDS_H
#define PLATFORM_BOARD_IDS_H
enum board_ntc_id {
	BOARD_NTC_OUTLET1 = 0,   /* PB11 */
	BOARD_NTC_POWER_STAGE,   /* PB10, staged heat feedback */
	BOARD_NTC_OUTLET2,       /* PB12 */
	BOARD_NTC_KETTLE,        /* PA5, maximum temperature limit */
	BOARD_NTC_COUNT,
};
#endif
