#include <assert.h>
#include "firmware/protocol.h"
#include "firmware/posture_logic.h"
#include "firmware/interaction.h"
#include "firmware/pose_countdown.h"
#include "firmware/pose_rules.h"
#include "firmware/screensaver_logic.h"

int main(void)
{
    char project[17]="original";
    uint16_t r=0,w=0,s=0; uint32_t seconds=0;
    assert(dashboard_metadata("M:P:abcdefghijklmnop",project,&r,&w,&s,&seconds));
    assert(!dashboard_metadata("M:P:abcdefghijklmnopq",project,&r,&w,&s,&seconds));
    assert(dashboard_metadata("M:C:999,998,997",project,&r,&w,&s,&seconds));
    assert(!dashboard_metadata("M:C:1,2,1000",project,&r,&w,&s,&seconds));
    assert(r==999 && w==998 && s==997);
    const char *p="4294967295"; uint32_t value;
    assert(dashboard_number(&p,UINT32_MAX,&value) && value==UINT32_MAX);
    p="4294967296"; assert(!dashboard_number(&p,UINT32_MAX,&value));
    dashboard_extra_t extra={0};
    assert(dashboard_extra("M:U:3,1,1",&extra) && extra.deep && extra.running && extra.mode==3);
    assert(!dashboard_extra("M:U:0,1,1",&extra));
    assert(!dashboard_extra("M:U:2,2,1",&extra));
    assert(dashboard_extra("M:D:9999,9999,9999",&extra));
    assert(!dashboard_extra("M:D:1,2,10000",&extra));
    assert(dashboard_extra("M:A:project-a",&extra));
    assert(!dashboard_extra("M:A:bad\nname",&extra));
    assert(dashboard_extra("M:N:99999999",&extra));
    assert(!dashboard_extra("M:N:100000000",&extra));

    pose_tracker_t tracker; pose_init(&tracker);
    assert(!pose_teach(&tracker,0));
    for (unsigned i=0;i<20;i++) pose_feed(&tracker,1,0,0,i*100);
    assert(pose_teach(&tracker,0));
    assert(!pose_teach(&tracker,1)); // indistinguishable poses are rejected
    for (unsigned i=0;i<20;i++) pose_feed(&tracker,0,1,0,2000+i*100);
    assert(pose_teach(&tracker,1));
    for (unsigned i=0;i<20;i++) pose_feed(&tracker,0,0,1,4000+i*100);
    assert(pose_teach(&tracker,2));
    unsigned flat_events=0;
    for (unsigned i=0;i<50;i++) if(pose_feed(&tracker,0,0,1,6000+i*100)==2) flat_events++;
    assert(flat_events==1);
    pose_feed(&tracker,0,0,2,12000); assert(!tracker.valid);
    pose_feed(&tracker,NAN,0,0,12100); assert(!tracker.valid);
    unsigned dev_events=0;
    for(unsigned i=0;i<50;i++) if(pose_feed(&tracker,0,1,0,13000+i*100)==1) dev_events++;
    assert(dev_events==1);
    assert(dashboard_extra("M:U:4,0,1",&extra) && extra.mode==4);
    assert(!dashboard_extra("M:U:5,0,1",&extra));
    assert(dashboard_extra("M:R:9999",&extra) && extra.rest_minutes==9999);
    assert(!dashboard_extra("M:R:10000",&extra));
    assert(dashboard_extra("M:S:1",&extra) && extra.stopped);
    assert(!dashboard_extra("M:S:2",&extra));
    pose_init(&tracker);
    const float directions[5][3]={{0,1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
    for(unsigned slot=0;slot<5;slot++) {
        for(unsigned tick=0;tick<20;tick++)
            pose_feed(&tracker,directions[slot][0],directions[slot][1],directions[slot][2],slot*2000+tick*100);
        assert(pose_teach(&tracker,slot));
    }
    unsigned last_events=0;
    for(unsigned tick=0;tick<50;tick++) if(pose_feed(&tracker,0,0,-1,10000+tick*100)==4) last_events++;
    assert(last_events==1);
    assert(screen_swipe(-90,10)==1 && screen_swipe(90,10)==-1);
    assert(screen_swipe(-50,0)==0 && screen_swipe(90,70)==0);
    assert(screen_next_page(0,-1)==2 && screen_next_page(2,1)==0);
    const int expected[4][2]={{30,100},{100,449},{449,379},{379,30}};
    for(unsigned q=0;q<4;q++) {
        int x=30,y=100;screen_touch_rotate(&x,&y,q,480);
        assert(x==expected[q][0] && y==expected[q][1]);
        screen_touch_rotate(&x,&y,(4-q)%4,480);
        assert(x==30 && y==100);
    }
    assert(pose_countdown_seconds(100,100)==3);
    assert(pose_countdown_seconds(100,1099)==3);
    assert(pose_countdown_seconds(100,1100)==2);
    assert(pose_countdown_seconds(100,2100)==1);
    assert(!pose_countdown_finished(100,3099));
    assert(pose_countdown_finished(100,3100));
    assert(pose_countdown_seconds(100,3100)==1);
    const char *actions[5]={"PAUSE","STUDY","DEV","WORK","REST"};
    const unsigned angles[5]={3,0,2,3,3};
    for(unsigned pose=0;pose<5;pose++) {
        assert(!strcmp(pose_activity_command(pose),actions[pose]));
        assert(pose_panel_rotation(pose)==angles[pose]);
        assert(pose_needs_countdown(pose)==(pose!=0));
    }
    assert(pose_screen_rotation(1)==1); // Physical left -> screen clockwise.
    assert(pose_screen_rotation(2)==3); // Physical right -> screen counterclockwise.
    assert(panel_rotation_from_relative(0)==3); // Preserve boot MADCTL A0.
    assert(panel_rotation_from_relative(1)==0); // Boot A0 + right 90 -> MADCTL 00.
    assert(panel_rotation_from_relative(3)==2); // Boot A0 + left 90 -> MADCTL C0.
    assert(!screensaver_due(100,60099,false));
    assert(screensaver_due(100,60100,false));
    assert(!screensaver_due(100,60100,true));
    assert(!screensaver_due(100,99,false));
    assert(screensaver_swipe(10,-90)==1);
    assert(screensaver_swipe(10,90)==-1);
    assert(screensaver_swipe(90,10)==0);
    assert(screensaver_swipe(60,90)==0);
    assert(screensaver_next(2,1)==0 && screensaver_next(0,-1)==2);
    assert(dashboard_extra("M:K:20261011,86399",&extra));
    assert(extra.clock_valid && extra.clock_seconds==86399 && extra.clock_date==20261011);
    assert(!dashboard_extra("M:K:20261011,86400",&extra));
    assert(!dashboard_extra("M:K:20260229,0",&extra));
    assert(dashboard_extra("M:K:20280229,0",&extra));
    assert(!dashboard_extra("M:K:20261301,0",&extra));
    assert(dashboard_extra("M:F:9999",&extra) && extra.deep_minutes==9999);
    assert(!dashboard_extra("M:F:10000",&extra));
    return 0;
}
