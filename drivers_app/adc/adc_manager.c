#include "adc_manager.h"
#include <errno.h>
#include <platform/board_devices.h>
static bool adc_ready;
int adc_manager_init(void)
{
    adc_ready = board_device_ready(board_get_resources()->adc_dev);
    return adc_ready ? 0 : -ENODEV;
}
int adc_manager_sample_all(struct adc_manager_sample *sample)
{
    if (!adc_ready || !sample) return -EACCES;
    for (unsigned i = 0; i < BOARD_NTC_COUNT; ++i) {
        int ret = board_adc_read(board_get_ntc((enum board_ntc_id)i), &sample->raw[i]);
        if (ret) return ret;
    }
    sample->raw_max = 4095;
    return 0;
}
