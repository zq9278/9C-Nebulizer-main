#include <assert.h>
#include <stdio.h>
#include <services/mist/mist_water_filter.h>
int main(void) {
    mist_water_filter_t f={0};
    assert(!mist_water_filter_report(&f,true,0));
    assert(!mist_water_filter_report(&f,true,19999));
    assert(!mist_water_filter_report(&f,false,19999));
    assert(!mist_water_filter_report(&f,true,19999));
    assert(!mist_water_filter_report(&f,true,39998));
    assert(mist_water_filter_report(&f,true,39999));
    assert(mist_water_filter_report(&f,false,40000));
    assert(mist_water_filter_report(&f,false,44999));
    assert(mist_water_filter_report(&f,true,44999));
    assert(mist_water_filter_report(&f,false,44999));
    assert(mist_water_filter_report(&f,false,49998));
    assert(!mist_water_filter_report(&f,false,49999));
    assert(!mist_water_filter_report(&f,true,50000));
    f.pending=false; /* offline */
    assert(!mist_water_filter_report(&f,true,80000));
    assert(mist_water_filter_report(&f,true,100000));
    assert(mist_water_filter_report(&f,false,100001));
    f.pending=false;
    assert(mist_water_filter_report(&f,false,110000));
    assert(!mist_water_filter_report(&f,false,115000));
    f=(mist_water_filter_t){0};
    uint32_t start=UINT32_MAX-100;
    assert(!mist_water_filter_report(&f,true,start));
    assert(!mist_water_filter_report(&f,true,start+19999U));
    assert(mist_water_filter_report(&f,true,start+20000U));
    puts("test_mist_water_filter: PASS (20s/5s, opposite report, offline, wrap)");
}
