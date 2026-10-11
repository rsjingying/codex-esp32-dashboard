#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static inline bool screensaver_due(uint64_t last_touch,uint64_t now,bool blocked)
{ return !blocked && now>=last_touch && now-last_touch>=60000; }
static inline int screensaver_swipe(int dx,int dy)
{
    if(abs(dy)<65 || abs(dy)<2*abs(dx)) return 0;
    return dy<0?1:-1;
}
static inline unsigned screensaver_next(unsigned style,int step)
{ return (unsigned)((int)style+step+3)%3; }
