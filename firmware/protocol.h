#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ASCII metadata, each packet fits the default 20-byte ATT payload. */
static inline bool dashboard_number(const char **cursor, uint32_t limit, uint32_t *out)
{
    const char *p = *cursor;
    uint32_t value = 0;
    if (*p < '0' || *p > '9') return false;
    do {
        uint32_t digit = (uint32_t)(*p++ - '0');
        if (digit > limit || value > (limit - digit) / 10) return false;
        value = value * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    *cursor = p;
    *out = value;
    return true;
}

static inline bool dashboard_metadata(const char *text, char project[17],
                                     uint16_t *running, uint16_t *waiting,
                                     uint16_t *seen, uint32_t *elapsed)
{
    if (!strncmp(text, "M:P:", 4)) {
        size_t len = strlen(text + 4);
        if (!len || len > 16) return false;
        for (size_t i = 4; i < len + 4; i++)
            if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126) return false;
        memcpy(project, text + 4, len + 1);
        return true;
    }
    if (!strncmp(text, "M:C:", 4)) {
        const char *p = text + 4;
        uint32_t r, w, s;
        if (!dashboard_number(&p, 999, &r) || *p != ',') return false;
        p++;
        if (!dashboard_number(&p, 999, &w) || *p != ',') return false;
        p++;
        if (!dashboard_number(&p, 999, &s) || *p) return false;
        *running = r; *waiting = w; *seen = s;
        return true;
    }
    if (!strncmp(text, "M:T:", 4)) {
        const char *p = text + 4;
        uint32_t seconds;
        if (!dashboard_number(&p, 99999999, &seconds) || *p) return false;
        *elapsed = seconds;
        return true;
    }
    return false;
}

typedef struct {
    char attention_project[17];
    uint32_t alert;
    uint32_t activity_elapsed, clock_date, clock_seconds;
    uint16_t work_minutes, study_minutes, dev_minutes, rest_minutes, deep_minutes;
    uint8_t mode;
    bool deep, running, valid, stopped, clock_valid;
} dashboard_extra_t;

static inline bool dashboard_triple(const char *text, uint32_t limit,
                                    uint32_t *a, uint32_t *b, uint32_t *c)
{
    const char *p = text;
    if (!dashboard_number(&p, limit, a) || *p != ',') return false;
    p++;
    if (!dashboard_number(&p, limit, b) || *p != ',') return false;
    p++;
    return dashboard_number(&p, limit, c) && !*p;
}

static inline bool dashboard_extra(const char *text, dashboard_extra_t *out)
{
    if (!strncmp(text,"M:K:",4)) {
        const char *p=text+4;uint32_t date,seconds;
        if(!dashboard_number(&p,21991231,&date) || *p++!=',' ||
           !dashboard_number(&p,86399,&seconds) || *p || date<20000101) return false;
        unsigned month=(date/100)%100,day=date%100,year=date/10000;
        const unsigned days[12]={31,28,31,30,31,30,31,31,30,31,30,31};
        if(month<1 || month>12 || day<1) return false;
        unsigned limit=days[month-1]+(month==2 && (year%4==0 && (year%100!=0 || year%400==0)));
        if(day>limit) return false;
        out->clock_date=date;out->clock_seconds=seconds;out->clock_valid=true;return true;
    }
    if (!strncmp(text, "M:A:", 4)) {
        size_t len = strlen(text + 4);
        if (!len || len > 16) return false;
        for (size_t i = 4; i < len + 4; i++)
            if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126) return false;
        memcpy(out->attention_project, text + 4, len + 1);
        return true;
    }
    if (!strncmp(text, "M:N:", 4) || !strncmp(text, "M:E:", 4)) {
        const char *p = text + 4;
        uint32_t value;
        if (!dashboard_number(&p, 99999999, &value) || *p) return false;
        if (text[2] == 'N') out->alert = value;
        else out->activity_elapsed = value;
        return true;
    }
    uint32_t a, b, c;
    if (!strncmp(text, "M:R:", 4) || !strncmp(text, "M:S:", 4) || !strncmp(text,"M:F:",4)) {
        const char *p = text + 4;
        uint32_t value;
        if (!dashboard_number(&p,text[2]=='S'?1:9999,&value) || *p) return false;
        if (text[2]=='R') out->rest_minutes=value;
        else if(text[2]=='F') out->deep_minutes=value;
        else out->stopped=value;
        return true;
    }
    if (!strncmp(text, "M:U:", 4)) {
        if (!dashboard_triple(text + 4, 4, &a, &b, &c) || a < 1 || b > 1 || c > 1)
            return false;
        out->mode = a; out->deep = b; out->running = c; out->valid = true;
        return true;
    }
    if (!strncmp(text, "M:D:", 4)) {
        if (!dashboard_triple(text + 4, 9999, &a, &b, &c)) return false;
        out->work_minutes = a; out->study_minutes = b; out->dev_minutes = c;
        return true;
    }
    return false;
}
