#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "ble_status.h"
#include "posture.h"
#include "pose_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "interaction.h"
#include "pose_countdown.h"
#include "pose_rules.h"
#include "screensaver_logic.h"

static QueueHandle_t updates,samples;
static dashboard_message_t current;
static pose_tracker_t tracker;
static lv_obj_t *pages[3],*badge,*footer,*page_title,*controls;
static lv_obj_t *ai_status,*ai_project,*ai_time,*ai_counts;
static lv_obj_t *attention_title,*attention_project,*attention_note;
static lv_obj_t *activity_mode,*activity_time,*activity_note,*activity_totals;
static lv_obj_t *pose_hint;
static int pending_pose=-1;
static uint64_t pending_at;
static char pose_feedback[48]="Pose: waiting for IMU";
static lv_obj_t *pause_text,*deep_text,*pose_toggle_text,*pose_captions[5],*binding_labels[5];
static lv_indev_t *touch;
static lv_indev_read_cb_t original_touch_read;
static unsigned page,rotation;
static lv_obj_t *saver_root,*saver_content,*saver_styles[3],*saver_time[2],*saver_mode[2],*saver_date;
static lv_obj_t *saver_ring,*saver_bar[4],*saver_total[4],*saver_deep,*saver_today_time;
static bool saver_active,press_was_saver,wake_pending;
static unsigned saver_style;
static int saver_nav,saver_brightness;
static uint64_t last_interaction,clock_at,saver_rendered_second;
static bool pose_enabled,controls_visible,pause_held,finish_held;
static uint64_t notice_until,controls_until;
static uint32_t shown_alert;
static int nav_pending;
static int teach_pending=-1;
static uint64_t teach_at,last_sample_ms;
static bool touch_held,touch_swiped;
static int touch_start_x,touch_start_y;
static const char *app_names[]={"IDLE","WORKING","WAITING","STOPPED","ERROR","UNKNOWN"};
static const char *mode_names[]={"-","WORK","STUDY","AI DEV","REST"};
static const unsigned colors[]={0x94A3B8,0x38BDF8,0xFBBF24,0x34D399,0xFB7185,0x94A3B8};
static const char *pose_names[]={"Normal","Left 90","Right 90","Face up","Face down"};

static uint64_t now_ms(void) { return (uint64_t)(esp_timer_get_time()/1000); }
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int width,
                      const lv_font_t *font,unsigned color)
{
    lv_obj_t *obj=lv_label_create(parent); lv_label_set_text(obj,text);
    lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,width);
    lv_label_set_long_mode(obj,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(obj,font,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    return obj;
}
static lv_obj_t *box(lv_obj_t *parent,int x,int y,int width,int height)
{
    lv_obj_t *obj=lv_obj_create(parent); lv_obj_set_pos(obj,x,y); lv_obj_set_size(obj,width,height);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x131C2B),0);
    lv_obj_set_style_border_width(obj,0,0); lv_obj_set_style_radius(obj,16,0);
    lv_obj_set_style_pad_all(obj,16,0); lv_obj_remove_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_EVENT_BUBBLE);
    return obj;
}
static void refresh(void);
static void saver_wake(void)
{
    last_interaction=now_ms();
    if(!saver_active) return;
    saver_active=false;
    if(saver_root) lv_obj_add_flag(saver_root,LV_OBJ_FLAG_HIDDEN);
    bsp_display_brightness_set(saver_brightness);
}
static void saver_refresh(void)
{
    if(!saver_active) return;
    static const unsigned activity_colors[]={0x94A3B8,0x38BDF8,0x34D399,0xA78BFA,0xFB923C};
    dashboard_extra_t *e=&current.extra;
    unsigned color=activity_colors[e->mode<=4?e->mode:0];
    unsigned phase=(unsigned)(now_ms()%5000);
    unsigned breathing=phase<2500?phase:5000-phase;
    lv_obj_set_style_arc_opa(saver_ring,(lv_opa_t)(50+breathing*150/2500),LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(saver_ring,lv_color_hex(color),LV_PART_INDICATOR);
    uint64_t second=now_ms()/1000;
    if(second==saver_rendered_second) return;
    saver_rendered_second=second;
    char text[96],time_text[32];
    if(e->running) {
        unsigned long s=e->activity_elapsed;
        snprintf(time_text,sizeof(time_text),"%02lu:%02lu:%02lu",s/3600,(s/60)%60,s%60);
    } else if(e->clock_valid) {
        uint32_t s=(e->clock_seconds+(uint32_t)((now_ms()-clock_at)/1000))%86400;
        snprintf(time_text,sizeof(time_text),"%02u:%02u:%02u",(unsigned)(s/3600),(unsigned)((s/60)%60),(unsigned)(s%60));
    } else strcpy(time_text,"--:--:--");
    for(unsigned i=0;i<2;i++) {
        lv_label_set_text(saver_time[i],time_text);
        lv_obj_set_style_text_color(saver_time[i],lv_color_hex(i?color:0xF1F5F9),0);
        snprintf(text,sizeof(text),"%s%s",e->mode<=4?mode_names[e->mode]:"-",e->running?" / RUNNING":" / PAUSED");
        lv_label_set_text(saver_mode[i],text);
    }
    if(e->clock_valid && current.connected) snprintf(text,sizeof(text),"%04u / %02u / %02u",(unsigned)(e->clock_date/10000),(unsigned)((e->clock_date/100)%100),(unsigned)(e->clock_date%100));
    else strcpy(text,current.connected?"Waiting for Mac time":"OFFLINE / estimated time");
    lv_label_set_text(saver_date,text);
    uint16_t mins[4]={e->work_minutes,e->study_minutes,e->dev_minutes,e->rest_minutes};
    unsigned max=1;for(unsigned i=0;i<4;i++) if(mins[i]>max) max=mins[i];
    for(unsigned i=0;i<4;i++) {
        snprintf(text,sizeof(text),"%um",mins[i]);lv_label_set_text(saver_total[i],text);
        lv_bar_set_value(saver_bar[i],mins[i]*100/max,LV_ANIM_OFF);
    }
    snprintf(text,sizeof(text),"Deep focus %um",e->deep_minutes);lv_label_set_text(saver_deep,text);
    snprintf(text,sizeof(text),"%s  %s",e->mode<=4?mode_names[e->mode]:"-",time_text);lv_label_set_text(saver_today_time,text);
    static const int drift_x[4]={-4,4,4,-4},drift_y[4]={-4,-4,4,4};
    unsigned drift=(unsigned)((now_ms()/60000)%4);
    lv_obj_set_pos(saver_content,40+drift_x[drift],40+drift_y[drift]);
}
static void saver_style_set(unsigned style)
{
    saver_style=style;
    for(unsigned i=0;i<3;i++) {
        if(i==style) lv_obj_remove_flag(saver_styles[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(saver_styles[i],LV_OBJ_FLAG_HIDDEN);
    }
    saver_rendered_second=UINT64_MAX;saver_refresh();
}
static void saver_enter(void)
{
    saver_brightness=bsp_display_brightness_get();
    if(saver_brightness<1) saver_brightness=100;
    saver_active=true;saver_style_set(saver_style);
    lv_obj_remove_flag(saver_root,LV_OBJ_FLAG_HIDDEN);
    bsp_display_brightness_set(saver_brightness<20?saver_brightness:20);
}
static void saver_create(lv_obj_t *screen)
{
    saver_root=lv_obj_create(screen);lv_obj_remove_style_all(saver_root);
    lv_obj_set_size(saver_root,480,480);lv_obj_set_style_bg_color(saver_root,lv_color_hex(0),0);
    lv_obj_set_style_bg_opa(saver_root,LV_OPA_COVER,0);
    lv_obj_remove_flag(saver_root,LV_OBJ_FLAG_SCROLLABLE);
    saver_content=lv_obj_create(saver_root);lv_obj_remove_style_all(saver_content);
    lv_obj_set_size(saver_content,400,400);lv_obj_set_pos(saver_content,40,40);
    lv_obj_remove_flag(saver_content,LV_OBJ_FLAG_SCROLLABLE);
    for(unsigned i=0;i<3;i++) {
        saver_styles[i]=lv_obj_create(saver_content);lv_obj_remove_style_all(saver_styles[i]);
        lv_obj_set_size(saver_styles[i],400,400);lv_obj_remove_flag(saver_styles[i],LV_OBJ_FLAG_SCROLLABLE);
    }
    saver_ring=lv_arc_create(saver_styles[1]);lv_obj_set_pos(saver_ring,45,45);lv_obj_set_size(saver_ring,310,310);
    lv_arc_set_range(saver_ring,0,100);lv_arc_set_value(saver_ring,100);
    lv_arc_set_bg_angles(saver_ring,0,360);lv_obj_remove_style(saver_ring,NULL,LV_PART_KNOB);
    lv_obj_remove_flag(saver_ring,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(saver_ring,5,LV_PART_MAIN);lv_obj_set_style_arc_width(saver_ring,5,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(saver_ring,lv_color_hex(0x101827),LV_PART_MAIN);
    for(unsigned i=0;i<2;i++) {
        saver_time[i]=label(saver_styles[i],"--:--:--",0,i?165:140,400,&lv_font_montserrat_48,0xF1F5F9);
        saver_mode[i]=label(saver_styles[i],"PAUSED",0,i?230:210,400,&lv_font_montserrat_20,0x94A3B8);
        lv_obj_set_style_text_align(saver_time[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_align(saver_mode[i],LV_TEXT_ALIGN_CENTER,0);
    }
    saver_date=label(saver_styles[0],"Waiting for Mac time",0,250,400,&lv_font_montserrat_20,0x64748B);
    lv_obj_set_style_text_align(saver_date,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *title=label(saver_styles[2],"TODAY",0,35,400,&lv_font_montserrat_28,0xF1F5F9);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
    const char *names[4]={"Work","Study","AI Dev","Rest"};
    for(unsigned i=0;i<4;i++) {
        int y=95+(int)i*52;
        label(saver_styles[2],names[i],12,y,100,&lv_font_montserrat_20,0xCBD5E1);
        saver_total[i]=label(saver_styles[2],"0m",305,y,85,&lv_font_montserrat_20,0xCBD5E1);
        lv_obj_set_style_text_align(saver_total[i],LV_TEXT_ALIGN_RIGHT,0);
        saver_bar[i]=lv_bar_create(saver_styles[2]);lv_obj_set_pos(saver_bar[i],112,y+5);lv_obj_set_size(saver_bar[i],180,12);
        lv_bar_set_range(saver_bar[i],0,100);
        const unsigned colors4[4]={0x38BDF8,0x34D399,0xA78BFA,0xFB923C};
        lv_obj_set_style_bg_color(saver_bar[i],lv_color_hex(colors4[i]),LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(saver_bar[i],lv_color_hex(0x18202D),LV_PART_MAIN);
    }
    saver_deep=label(saver_styles[2],"Deep focus 0m",0,320,400,&lv_font_montserrat_20,0x94A3B8);
    saver_today_time=label(saver_styles[2],"",0,355,400,&lv_font_montserrat_20,0x64748B);
    lv_obj_set_style_text_align(saver_deep,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_align(saver_today_time,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_add_flag(saver_root,LV_OBJ_FLAG_HIDDEN);
}

static void reveal_controls(void)
{
    controls_visible=true; controls_until=now_ms()+6000;
    lv_obj_remove_flag(controls,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(footer,LV_OBJ_FLAG_HIDDEN);
    refresh();
}
static void show_page(unsigned index)
{
    saver_wake();
    if(page==2 && index!=2) tracker.candidate=tracker.committed=-2;
    if(index!=1 && pending_at) {pending_pose=-1;pending_at=0;}
    page=index;
    if(index!=2) teach_pending=-1;
    if(index==2) {
        pending_pose=-1;pending_at=0;
        // The settings page suspends actions without changing the saved enable flag.
    }
    for(unsigned i=0;i<3;i++) {
        if(i==index) lv_obj_remove_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i],LV_OBJ_FLAG_HIDDEN);
    }
    const char *titles[]={"AI   1 / 3","TIMER   2 / 3","POSE   3 / 3"};
    lv_label_set_text(page_title,titles[index]);
    if(index==1) {
        controls_visible=false;
        lv_obj_add_flag(controls,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(page_title,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(footer,LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(page_title,LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(footer,LV_OBJ_FLAG_HIDDEN);
    }
}
static void notice(const char *text)
{
    notice_until=now_ms()+4000; lv_label_set_text(footer,text);
    if(page==1 && controls_visible) lv_obj_remove_flag(footer,LV_OBJ_FLAG_HIDDEN);
}
static bool send_command(const char *command)
{
    if(!current.connected || !current.extra.valid) { notice("Start the V5 bridge on Mac"); return false; }
    if(!dashboard_send_command(command)) { notice("Please wait for Mac..."); return false; }
    notice("Sending to Mac..."); return true;
}
static void rotate_screen(unsigned clockwise)
{
    if(clockwise==rotation) return;
    unsigned absolute=panel_rotation_from_relative(clockwise);
    esp_err_t err=bsp_display_rotation_set((bsp_display_rotation_t)absolute);
    if(err!=ESP_OK) {notice("Display rotation failed");return;}
    rotation=clockwise;
    if(touch) lv_indev_reset(touch,NULL);
    lv_obj_invalidate(lv_screen_active());
}
static void touch_read(lv_indev_t *indev,lv_indev_data_t *data)
{
    original_touch_read(indev,data);
    int x=data->point.x,y=data->point.y;
    screen_touch_rotate(&x,&y,rotation,480);
    data->point.x=x; data->point.y=y;
    if(data->state==LV_INDEV_STATE_PRESSED) {
        if(!touch_held) {
            touch_held=true;touch_swiped=false;touch_start_x=x;touch_start_y=y;
            press_was_saver=saver_active;
        }
        last_interaction=now_ms();
        if(press_was_saver) {
            if(!touch_swiped) {
                int step=screensaver_swipe(x-touch_start_x,y-touch_start_y);
                if(step) {touch_swiped=true;saver_nav=step;}
                else if(abs(x-touch_start_x)>=65) touch_swiped=true;
            }
            lv_indev_wait_release(indev);return;
        }
        if(!touch_swiped) {
            int step=screen_swipe(x-touch_start_x,y-touch_start_y);
            if(step) {
                touch_swiped=true;nav_pending=step;
                lv_indev_wait_release(indev);
            }
        }
        if(page==1 && controls_visible) controls_until=now_ms()+6000;
    } else {
        if(touch_held && press_was_saver && !touch_swiped) wake_pending=true;
        touch_held=false;press_was_saver=false;
    }
}
static void screen_tap(lv_event_t *event)
{
    (void)event;
    if(!saver_active && page==1 && !touch_swiped && !pending_at) reveal_controls();
}
static void action(lv_event_t *event)
{
    const char *command=lv_event_get_user_data(event);
    if(!strcmp(command,"DEEP_TOGGLE")) command=current.extra.deep?"DEEP0":"DEEP1";
    if(send_command(command)) controls_until=now_ms()+6000;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,const char *command,
                        int x,int y,int width,int height)
{
    lv_obj_t *obj=lv_button_create(parent);lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,width,height);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x23436A),0);
    lv_obj_set_style_radius(obj,12,0);lv_obj_set_style_shadow_width(obj,0,0);
    lv_obj_add_event_cb(obj,action,LV_EVENT_CLICKED,(void *)command);
    lv_obj_add_flag(obj,LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *caption=lv_label_create(obj);lv_label_set_text(caption,text);
    lv_obj_set_style_text_font(caption,&lv_font_montserrat_20,0);lv_obj_center(caption);
    return caption;
}
static void pause_action(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED) pause_held=false;
    else if(code==LV_EVENT_LONG_PRESSED) { pause_held=true;send_command("STOP"); }
    else if(code==LV_EVENT_CLICKED) {
        if(!pause_held) send_command(current.extra.running?"PAUSE":"RESUME");
        pause_held=false;
    }
}
static void finish_action(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    if(code==LV_EVENT_PRESSED) finish_held=false;
    else if(code==LV_EVENT_LONG_PRESSED) {finish_held=true;send_command("FINISH");}
    else if(code==LV_EVENT_CLICKED) {
        if(!finish_held) notice("Hold Finish for the daily note");
        finish_held=false;
    }
}
static void teach_pose(lv_event_t *event)
{
    unsigned slot=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    // Recalibration explicitly locks automation until the user enables it again.
    if(pose_enabled) {
        esp_err_t err=pose_storage_save(&tracker,false);
        if(err!=ESP_OK) {notice("Storage failed / retry");return;}
        pose_enabled=false;
        lv_label_set_text(pose_toggle_text,"Pose OFF / tap ON");
    }
    teach_pending=(int)slot;teach_at=now_ms()+5000;
    char text[64];snprintf(text,sizeof(text),"Place %s / hold still for 5s",pose_names[slot]);
    notice(text);notice_until=teach_at+100;
}
static void toggle_pose(lv_event_t *event)
{
    (void)event;
    for(unsigned i=0;i<5;i++) if(!tracker.taught[i]) {notice("Teach Normal and all 4 directions");return;}
    if(teach_pending>=0) {notice("Wait for pose capture");return;}
    esp_err_t err=pose_storage_save(&tracker,!pose_enabled);
    if(err!=ESP_OK) {notice("Storage failed / switch unchanged");return;}
    pose_enabled=!pose_enabled;tracker.candidate=tracker.committed=-2;pending_pose=-1;pending_at=0;
    lv_label_set_text(pose_toggle_text,pose_enabled?"Pose ON":"Pose OFF / tap ON");
    if(pose_enabled) { show_page(1); refresh(); }
}
static void clear_pose(lv_event_t *event)
{
    static bool held;
    if(lv_event_get_code(event)==LV_EVENT_PRESSED) {held=false;return;}
    if(lv_event_get_code(event)==LV_EVENT_CLICKED) {
        if(!held) notice("Hold Clear to erase all poses");
        held=false;return;
    }
    if(lv_event_get_code(event)!=LV_EVENT_LONG_PRESSED) return;
    held=true;
    esp_err_t err=pose_storage_clear();
    if(err!=ESP_OK) {notice("Storage failed / poses retained");return;}
    pose_init(&tracker);pose_enabled=false;teach_pending=-1;
    pending_pose=-1;pending_at=0;
    for(unsigned i=0;i<5;i++) lv_label_set_text(pose_captions[i],pose_names[i]);
    lv_label_set_text(pose_toggle_text,"Pose OFF / tap ON");
    notice("Calibration cleared / teach again");
}
static void duration(lv_obj_t *obj,uint32_t seconds,const char *prefix)
{
    char text[64];unsigned long s=seconds;
    snprintf(text,sizeof(text),"%s%02lu:%02lu:%02lu",prefix,s/3600,(s/60)%60,s%60);
    lv_label_set_text(obj,text);
}
static void refresh(void)
{
    dashboard_extra_t *extra=&current.extra;
    const char *status=!current.connected?"OFFLINE":!current.has_status?"CONNECTED":
                       current.state<6?app_names[current.state]:"UNKNOWN";
    lv_label_set_text(ai_status,status);
    lv_obj_set_style_text_color(ai_status,lv_color_hex(current.state<6?colors[current.state]:0x94A3B8),0);
    lv_label_set_text(ai_project,current.project[0]?current.project:"Project: -");
    duration(ai_time,current.elapsed,"Turn ");
    char text[144];
    snprintf(text,sizeof(text),"Run %u   Wait %u   Seen %u",current.running,current.waiting,current.seen);
    lv_label_set_text(ai_counts,text);
    snprintf(text,sizeof(text),"Needs you: %u",current.connected?current.waiting:0);
    lv_label_set_text(attention_title,text);
    lv_label_set_text(attention_project,current.connected && current.waiting?extra->attention_project:"No approval requests");
    lv_label_set_text(attention_note,current.connected && current.waiting?"Approval / tap Seen":"Observed local events");
    lv_label_set_text(activity_mode,extra->mode<=4?mode_names[extra->mode]:"-");
    if(pending_pose>=0 && pending_at) {
        char countdown[16];snprintf(countdown,sizeof(countdown),"%u",pose_countdown_seconds(pending_at,now_ms()));
        lv_label_set_text(activity_time,countdown);
        lv_label_set_text(activity_mode,pose_activity_caption((unsigned)pending_pose));
    } else duration(activity_time,extra->activity_elapsed,"");
    const char *activity_state=!current.connected?"Disconnected / paused":extra->running?
                      (extra->deep?"DEEP FOCUS":"Running"):extra->stopped?"Stopped / tap to start":"Paused / tap to resume";
    snprintf(text,sizeof(text),"%s%s%s",activity_state,pose_enabled?"\n":"",pose_enabled?pose_feedback:"");
    if(pending_pose>=0 && pending_at)
        snprintf(text,sizeof(text),"Starting / %s",pose_names[pending_pose]);
    lv_label_set_text(activity_note,text);
    if(pose_hint) lv_label_set_text(pose_hint,teach_pending>=0?"Place pose / hold still for 5s":pose_feedback);
    snprintf(text,sizeof(text),"Today W %um / S %um\nDev %um / Rest %um",extra->work_minutes,
             extra->study_minutes,extra->dev_minutes,extra->rest_minutes);
    lv_label_set_text(activity_totals,text);
    lv_label_set_text(pause_text,extra->running?"Pause":extra->stopped?"Start":"Resume");
    lv_label_set_text(deep_text,extra->deep?"Focus ON":"Focus OFF");
    lv_label_set_text(badge,!current.connected?"OFFLINE":extra->alert?"NEEDS YOU":"BLE");
    if(page==1 && !controls_visible && !extra->alert) lv_obj_add_flag(badge,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(badge,LV_OBJ_FLAG_HIDDEN);
    if(extra->alert && extra->alert!=shown_alert) {
        shown_alert=extra->alert;saver_wake();
        if(!extra->deep && page!=2) show_page(0);
    }
    if(!dashboard_command_pending() && now_ms()>notice_until)
        lv_label_set_text(footer,page==2?"Tap pose / place it within 5s":
                          "Swipe pages / hold Pause to stop");
}
static void poll(lv_timer_t *timer)
{
    (void)timer;
    if(wake_pending) {wake_pending=false;saver_wake();}
    if(saver_nav) {if(saver_active) saver_style_set(screensaver_next(saver_style,saver_nav));saver_nav=0;}
    if(nav_pending) {show_page(screen_next_page(page,nav_pending));nav_pending=0;refresh();}
    dashboard_message_t message;
    if(xQueueReceive(updates,&message,0)==pdTRUE) {
        if(message.extra.clock_valid && (!current.extra.clock_valid ||
           message.extra.clock_seconds!=current.extra.clock_seconds || message.extra.clock_date!=current.extra.clock_date)) clock_at=now_ms();
        current=message;refresh();
    }
    if(notice_until && now_ms()>notice_until) {notice_until=0;refresh();}
    if(page==1 && controls_visible && now_ms()>controls_until) {
        controls_visible=false;lv_obj_add_flag(controls,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(footer,LV_OBJ_FLAG_HIDDEN);refresh();
    }
    posture_sample_t sample;
    if(xQueueReceive(samples,&sample,0)==pdTRUE) {
        last_sample_ms=now_ms();
        int detected=pose_feed(&tracker,sample.valid?sample.x:NAN,sample.y,sample.z,now_ms());
        char feedback[48];
        snprintf(feedback,sizeof(feedback),"Pose: %s",!tracker.valid?"IMU invalid":
                 tracker.candidate>=0?pose_names[tracker.candidate]:"hold still / no match");
        if(strcmp(feedback,pose_feedback)) {strcpy(pose_feedback,feedback);refresh();}
        if(detected>=0 && tracker.taught[detected] && page!=2) {
            ESP_LOGI("dashboard_pose","Detected %s / enabled=%d",pose_names[detected],pose_enabled);
            saver_wake();rotate_screen(pose_screen_rotation((unsigned)detected));
            pending_pose=pose_enabled?detected:-1;pending_at=0;
        } else if(pending_pose>=0 && (!tracker.valid || tracker.candidate!=pending_pose || page==2)) {
            pending_pose=-1;pending_at=0;refresh();
        }
    }
    if(pending_at && (!current.connected || !current.extra.valid)) {pending_at=0;refresh();}
    // Keep a recognised action until the bridge is ready; do not consume it offline.
    if(pending_pose>=0 && pose_enabled && page!=2 && current.connected && current.extra.valid &&
       tracker.valid && tracker.candidate==pending_pose && now_ms()-last_sample_ms<300) {
        if(!pose_needs_countdown((unsigned)pending_pose)) {
            // Normal is a neutral pose: pause without starting a countdown or a new activity.
            if(dashboard_send_command("PAUSE")) {
                ESP_LOGI("dashboard_pose","Normal / paused");
                pending_pose=-1;pending_at=0;refresh();
            }
        } else if(!pending_at) {
            pending_at=now_ms();show_page(1);refresh();
        } else if(pose_countdown_finished(pending_at,now_ms())) {
            const char *command=pose_activity_command((unsigned)pending_pose);
            if(dashboard_send_command(command)) {
                ESP_LOGI("dashboard_pose","Queued %s for %s",command,pose_names[pending_pose]);
                pending_pose=-1;pending_at=0;refresh();
            }
        } else refresh();
    }

    if(teach_pending>=0 && now_ms()>=teach_at) {
        unsigned slot=(unsigned)teach_pending;teach_pending=-1;
        pose_tracker_t taught=tracker;
        if(now_ms()-last_sample_ms<300 && pose_teach(&taught,slot)) {
            esp_err_t err=pose_storage_save(&taught,pose_enabled);
            if(err!=ESP_OK) {
                ESP_LOGW("dashboard_pose","Save failed: %s",esp_err_to_name(err));
                notice("Storage failed / pose not saved");
                return;
            }
            tracker=taught;
            char text[48];snprintf(text,sizeof(text),"%s / saved",pose_names[slot]);
            lv_label_set_text(pose_captions[slot],text);notice("Pose saved");
        } else notice("Not saved / hold still, then retry");
    }
    if(!saver_active && screensaver_due(last_interaction,now_ms(),page==2 || touch_held ||
       pending_at || teach_pending>=0 || dashboard_command_pending() || current.extra.alert)) saver_enter();
    saver_refresh();
}
void app_main(void)
{
    ESP_LOGI("dashboard","CODEX V6.1");
    updates=xQueueCreate(1,sizeof(dashboard_message_t));samples=xQueueCreate(1,sizeof(posture_sample_t));
    assert(updates && samples);pose_init(&tracker);
    ESP_ERROR_CHECK(nvs_flash_init());
    esp_err_t pose_err=pose_storage_load(&tracker,&pose_enabled);
    if(pose_err==ESP_OK) ESP_LOGI("dashboard_pose","Calibration restored / enabled=%d",pose_enabled);
    else if(pose_err!=ESP_ERR_NVS_NOT_FOUND)
        ESP_LOGW("dashboard_pose","Calibration not restored: %s",esp_err_to_name(pose_err));
    bsp_display_start();bsp_display_lock(-1);
    touch=bsp_display_get_input_dev();
    if(touch) {
        original_touch_read=lv_indev_get_read_cb(touch);
        if(original_touch_read) lv_indev_set_read_cb(touch,touch_read);
    }
    lv_obj_t *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0x080C14),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(screen,screen_tap,LV_EVENT_CLICKED,NULL);
    page_title=label(screen,"AI  1 / 3",40,18,260,&lv_font_montserrat_20,0x94A3B8);
    badge=label(screen,"OFFLINE",310,18,130,&lv_font_montserrat_20,0xFBBF24);
    lv_obj_set_style_text_align(badge,LV_TEXT_ALIGN_RIGHT,0);
    for(unsigned i=0;i<3;i++) {
        pages[i]=lv_obj_create(screen);lv_obj_remove_style_all(pages[i]);
        lv_obj_set_pos(pages[i],40,58);lv_obj_set_size(pages[i],400,380);
        lv_obj_remove_flag(pages[i],LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(pages[i],LV_OBJ_FLAG_EVENT_BUBBLE);
    }
    lv_obj_t *card=box(pages[0],0,0,400,180);
    ai_status=label(card,"OFFLINE",0,0,368,&lv_font_montserrat_40,0x94A3B8);
    ai_project=label(card,"Project: -",0,52,368,&lv_font_montserrat_28,0xF1F5F9);
    ai_time=label(card,"Turn 00:00:00",0,91,368,&lv_font_montserrat_20,0x38BDF8);
    ai_counts=label(card,"Run 0   Wait 0   Seen 0",0,126,368,&lv_font_montserrat_20,0x94A3B8);
    card=box(pages[0],0,194,400,130);
    attention_title=label(card,"Needs you: 0",0,0,368,&lv_font_montserrat_20,0xFBBF24);
    attention_project=label(card,"No approval requests",0,29,368,&lv_font_montserrat_28,0xF1F5F9);
    attention_note=label(card,"Observed local events",0,75,254,&lv_font_montserrat_20,0x94A3B8);
    button(card,"Seen","SEEN",270,65,98,40);
    label(pages[0],"Swipe left for your timer",0,344,400,&lv_font_montserrat_20,0x94A3B8);
    activity_mode=label(pages[1],"WORK",0,55,400,&lv_font_montserrat_28,0xF1F5F9);
    activity_time=label(pages[1],"00:00:00",0,108,400,&lv_font_montserrat_48,0x38BDF8);
    activity_note=label(pages[1],"Tap for controls",0,174,400,&lv_font_montserrat_20,0x94A3B8);
    lv_obj_set_style_text_align(activity_mode,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_align(activity_time,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_align(activity_note,LV_TEXT_ALIGN_CENTER,0);
    lv_label_set_long_mode(activity_note,LV_LABEL_LONG_WRAP);
    controls=lv_obj_create(pages[1]);lv_obj_remove_style_all(controls);
    lv_obj_set_pos(controls,0,225);lv_obj_set_size(controls,400,155);
    lv_obj_remove_flag(controls,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(controls,LV_OBJ_FLAG_EVENT_BUBBLE);
    button(controls,"Work","WORK",0,0,124,42);button(controls,"Study","STUDY",138,0,124,42);
    button(controls,"AI Dev","DEV",276,0,124,42);
    pause_text=button(controls,"Start","RESUME",0,51,124,42);
    lv_obj_t *pause_button=lv_obj_get_parent(pause_text);lv_obj_remove_event_cb(pause_button,action);
    lv_obj_add_event_cb(pause_button,pause_action,LV_EVENT_ALL,NULL);
    deep_text=button(controls,"Focus OFF","DEEP_TOGGLE",138,51,124,42);
    lv_obj_t *finish_caption=button(controls,"Finish","FINISH",276,51,124,42);
    lv_obj_t *finish_button=lv_obj_get_parent(finish_caption);lv_obj_remove_event_cb(finish_button,action);
    lv_obj_add_event_cb(finish_button,finish_action,LV_EVENT_ALL,NULL);
    activity_totals=label(controls,"Today W 0m / S 0m\nDev 0m / Rest 0m",0,105,400,&lv_font_montserrat_20,0x94A3B8);
    lv_label_set_long_mode(activity_totals,LV_LABEL_LONG_WRAP);
    label(pages[2],"Pose shortcuts",0,0,400,&lv_font_montserrat_28,0xF1F5F9);
    pose_hint=label(pages[2],"Tap pose / 5s to place and hold",0,39,400,&lv_font_montserrat_20,0x94A3B8);
    for(unsigned i=0;i<5;i++) {
        pose_captions[i]=button(pages[2],pose_names[i],"",0,75+i*48,236,40);
        lv_obj_t *btn=lv_obj_get_parent(pose_captions[i]);lv_obj_remove_event_cb(btn,action);
        lv_obj_add_event_cb(btn,teach_pose,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
        binding_labels[i]=button(pages[2],pose_activity_caption(i),"",248,75+i*48,152,40);
        btn=lv_obj_get_parent(binding_labels[i]);lv_obj_remove_event_cb(btn,action);
        lv_obj_add_state(btn,LV_STATE_DISABLED);
    }
    for(unsigned i=0;i<5;i++) if(tracker.taught[i]) {
        char text[48];snprintf(text,sizeof(text),"%s / saved",pose_names[i]);
        lv_label_set_text(pose_captions[i],text);
    }
    pose_toggle_text=button(pages[2],pose_enabled?"Pose ON":"Pose OFF / tap ON","",0,325,248,42);
    lv_obj_t *toggle=lv_obj_get_parent(pose_toggle_text);lv_obj_remove_event_cb(toggle,action);
    lv_obj_add_event_cb(toggle,toggle_pose,LV_EVENT_CLICKED,NULL);
    lv_obj_t *clear_caption=button(pages[2],"Hold Clear","",260,325,140,42);
    lv_obj_t *clear_button=lv_obj_get_parent(clear_caption);lv_obj_remove_event_cb(clear_button,action);
    lv_obj_add_event_cb(clear_button,clear_pose,LV_EVENT_ALL,NULL);
    footer=label(screen,"Swipe left / right to change pages",40,450,400,&lv_font_montserrat_20,0x94A3B8);
    saver_create(screen);show_page(0);lv_timer_create(poll,100,NULL);bsp_display_unlock();
    ble_dashboard_start(updates);posture_start(samples);
}
