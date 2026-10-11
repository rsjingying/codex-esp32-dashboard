#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline bool pose_countdown_finished(uint64_t started, uint64_t now)
{ return now >= started && now-started >= 3000; }

static inline unsigned pose_countdown_seconds(uint64_t started, uint64_t now)
{
    uint64_t elapsed=now>=started?now-started:0;
    // Keep 1 visible while the final BLE command waits for its queue slot.
    return elapsed>=3000?1:(unsigned)((3000-elapsed+999)/1000);
}
