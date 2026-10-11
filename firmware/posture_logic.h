#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x, y, z; } pose_vector_t;
typedef struct {
    pose_vector_t poses[5], previous, latest;
    bool taught[5], have_previous, valid;
    unsigned still_samples;
    int candidate, committed;
    uint64_t since;
} pose_tracker_t;

static inline float pose_dot(pose_vector_t a, pose_vector_t b)
{ return a.x*b.x + a.y*b.y + a.z*b.z; }

static inline void pose_init(pose_tracker_t *p)
{ *p = (pose_tracker_t){ .candidate = -2, .committed = -2 }; }

static inline int pose_feed(pose_tracker_t *p, float x, float y, float z, uint64_t now)
{
    float length = sqrtf(x*x + y*y + z*z);
    p->valid = isfinite(length) && length > 0.8f && length < 1.2f;
    if (!p->valid) {
        p->still_samples = 0; p->have_previous = false; p->candidate = -2;
        return -1;
    }
    pose_vector_t v = {x/length, y/length, z/length};
    bool still = p->have_previous && pose_dot(v, p->previous) > 0.999f;
    p->still_samples = still ? p->still_samples + 1 : 0;
    p->previous = p->latest = v; p->have_previous = true;
    int candidate = -1;
    float best = 0.96f;
    if (p->still_samples >= 10) {
        for (int i = 0; i < 5; i++) {
            float similarity = pose_dot(v, p->poses[i]);
            if (p->taught[i] && similarity > best) { best = similarity; candidate = i; }
        }
    }
    if (candidate != p->candidate) { p->candidate = candidate; p->since = now; }
    if (now - p->since >= 2000 && candidate != p->committed) {
        p->committed = candidate;
        return candidate;
    }
    return -1;
}

static inline bool pose_teach(pose_tracker_t *p, unsigned slot)
{
    if (slot >= 5 || !p->valid || p->still_samples < 10) return false;
    for (unsigned i = 0; i < 5; i++)
        if (i != slot && p->taught[i] && pose_dot(p->latest, p->poses[i]) > 0.85f)
            return false;
    p->poses[slot] = p->latest; p->taught[slot] = true;
    p->candidate = p->committed = -2;
    return true;
}
