#pragma once
#include <stdbool.h>

/* Slots: Normal, Left 90, Right 90, Face up, Face down.
 * Screen rotations are clockwise relative to the BSP's boot orientation.
 * This board boots with MADCTL 0xA0 (BSP 270), not BSP 0. */
static inline unsigned pose_screen_rotation(unsigned pose)
{
    static const unsigned clockwise[5]={0,1,3,0,0};
    return pose<5?clockwise[pose]:0;
}
static inline unsigned panel_rotation_from_relative(unsigned clockwise)
{ return (3+clockwise)%4; }
static inline unsigned pose_panel_rotation(unsigned pose)
{ return panel_rotation_from_relative(pose_screen_rotation(pose)); }
static inline const char *pose_activity_command(unsigned pose)
{
    static const char *names[5]={"PAUSE","STUDY","DEV","WORK","REST"};
    return pose<5?names[pose]:"PAUSE";
}
static inline const char *pose_activity_caption(unsigned pose)
{
    static const char *names[5]={"No timer","Study","AI Dev","Work","Rest"};
    return pose<5?names[pose]:"No timer";
}
static inline bool pose_needs_countdown(unsigned pose)
{ return pose>0 && pose<5; }
