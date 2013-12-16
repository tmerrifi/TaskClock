
#include <stdint.h>

//Max size is 8 MB
#define DEBUG_CLOCK_CACHE_ARR_SIZE (1<<20)
#define DEBUG_CLOCK_CACHE_ARR_SIZE_BYTES ((1<<20)*8)

//we always add X% to the clock, since we can underestimate safely and still maintain correctness
#define DEBUG_CLOCK_CACHE_ERROR_ADJUSTMENT_PERCENTAGE .20D

struct debug_clock_cache{
    uint64_t count;
    uint64_t * clock_arr;
    int thread_id;
};

void debug_clock_cache_init(uint32_t thread_id, struct debug_clock_cache * clocks);

void debug_clock_cache_insert(struct debug_clock_cache * clocks, uint64_t clock_value, uint64_t * diff);

uint64_t debug_clock_cache_get(struct debug_clock_cache * clocks, uint64_t clock_value, uint64_t * diff);
