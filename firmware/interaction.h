#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
/* Inverse coordinates for the panel's clockwise MADCTL orientation. */
static inline void screen_touch_rotate(int *x,int *y,unsigned q,int extent)
{
    int a=*x,b=*y;
    switch(q%4) {
    case 1:*x=b;*y=extent-1-a;break;
    case 2:*x=extent-1-a;*y=extent-1-b;break;
    case 3:*x=extent-1-b;*y=a;break;
    default:break;
    }
}
static inline int screen_swipe(int dx,int dy)
{
    if(abs(dx)<65 || abs(dx)<2*abs(dy)) return 0;
    return dx<0?1:-1;
}
static inline unsigned screen_next_page(unsigned page,int step)
{ return (unsigned)((int)page+step+3)%3; }
