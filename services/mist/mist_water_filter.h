#ifndef MIST_WATER_FILTER_H
#define MIST_WATER_FILTER_H
#include <stdbool.h>
#include <stdint.h>
#include <nebulizer/app_config.h>
typedef struct { bool confirmed_low, pending; uint32_t since; } mist_water_filter_t;
/* Process EVERY report: even one opposite report cancels the candidate. */
static inline bool mist_water_filter_report(mist_water_filter_t *f, bool low, uint32_t now)
{
    if (low == f->confirmed_low) f->pending = false;
    else if (!f->pending) { f->pending = true; f->since = now; }
    else if ((uint32_t)(now - f->since) >=
             (low ? APP_MIST_LOW_WATER_CONFIRM_MS : APP_MIST_WATER_RESTORE_MS)) {
        f->confirmed_low = low; f->pending = false;
    }
    return f->confirmed_low;
}
#endif
