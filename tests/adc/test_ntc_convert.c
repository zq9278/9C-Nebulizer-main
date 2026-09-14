#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <drivers_app/adc/ntc_convert.h>
#include <drivers_app/adc/ntc_table.h>

static uint16_t raw_for_resistance(uint32_t deciohms)
{
    return (uint16_t)(((uint64_t)deciohms * 4095U + (100000U + deciohms) / 2U) /
        (100000U + deciohms));
}

int main(void)
{
    size_t count;
    const struct ntc_table_entry *table = ntc_table_get_outlet_3435(&count);
    assert(count == 341);
    assert(table[0].temp_deci_c == -400 && table[0].resistance_deciohms == 2033645);
    assert(table[65].temp_deci_c == 250 && table[65].resistance_deciohms == 100000);
    assert(table[82].temp_deci_c == 420 && table[82].resistance_deciohms == 54284);
    assert(table[125].temp_deci_c == 850 && table[125].resistance_deciohms == 14500);
    assert(table[340].temp_deci_c == 3000 && table[340].resistance_deciohms == 350);
    for (size_t i = 1; i < count; ++i) {
        assert(table[i].temp_deci_c == table[i - 1].temp_deci_c + 10);
        assert(table[i].resistance_deciohms < table[i - 1].resistance_deciohms);
    }
    /* Check every 1 C point in the outlet operating region against the document. */
    for (size_t i = 40; i <= 140; ++i) {
        uint16_t raw = raw_for_resistance(table[i].resistance_deciohms);
        for (unsigned ch = 0; ch < BOARD_NTC_COUNT; ch += 2) {
            struct ntc_convert_result r = ntc_convert_from_raw_for_channel(ch, raw, 4095, 10000);
            assert(!r.open_circuit && !r.short_circuit);
            assert(abs(r.temp_deci_c - table[i].temp_deci_c) <= 2);
        }
    }
    /* Same divider resistance: PB11/PB12 use new curve; PB10/PA5 keep old curve. */
    for (uint16_t raw = 0; raw <= 4095; ++raw) {
        struct ntc_convert_result old = ntc_convert_from_raw(raw, 4095, 10000);
        for (unsigned ch = 1; ch < BOARD_NTC_COUNT; ch += 2) {
            struct ntc_convert_result r = ntc_convert_from_raw_for_channel(ch, raw, 4095, 10000);
            assert(r.temp_deci_c == old.temp_deci_c);
            assert(r.open_circuit == old.open_circuit && r.short_circuit == old.short_circuit);
        }
    }
    for (unsigned ch = 0; ch < BOARD_NTC_COUNT; ++ch) {
        assert(ntc_convert_from_raw_for_channel(ch, 8, 4095, 10000).short_circuit);
        assert(ntc_convert_from_raw_for_channel(ch, 4087, 4095, 10000).open_circuit);
        assert(!ntc_convert_from_raw_for_channel(ch, 9, 4095, 10000).short_circuit);
        assert(!ntc_convert_from_raw_for_channel(ch, 4086, 4095, 10000).open_circuit);
    }
    /* 12-bit midscale cannot represent exactly half of 4095. */
    assert(abs(ntc_convert_from_raw_for_channel(BOARD_NTC_OUTLET1, 2048, 4095, 10000).temp_deci_c - 250) <= 1);
    assert(abs(ntc_convert_from_raw_for_channel(BOARD_NTC_OUTLET2, 2048, 4095, 10000).temp_deci_c - 250) <= 1);
    puts("test_ntc_convert: PASS (3435 curve, outlet routing, legacy channels, faults)");
    return 0;
}
