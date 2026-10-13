#pragma once
#include <string.h>
#include "posture_logic.h"

// Versioned, fixed-size record; only calibration and the user's enable flag persist.
typedef struct {
    uint32_t magic, version, mask, enabled;
    pose_vector_t poses[5];
} pose_settings_t;

static inline pose_settings_t pose_settings_capture(const pose_tracker_t *tracker, bool enabled)
{
    pose_settings_t record = { .magic = 0x504F5345, .version = 1, .enabled = enabled };
    for (unsigned i = 0; i < 5; i++) {
        if (tracker->taught[i]) {
            record.mask |= 1u << i;
            record.poses[i] = tracker->poses[i];
        }
    }
    return record;
}

static inline bool pose_settings_restore(const pose_settings_t *record,
                                         pose_tracker_t *tracker, bool *enabled)
{
    if (record->magic != 0x504F5345 || record->version != 1 || record->mask > 31 ||
        record->enabled > 1 || (record->enabled && record->mask != 31)) return false;
    for (unsigned i = 0; i < 5; i++) {
        if (!(record->mask & (1u << i))) continue;
        float length = pose_dot(record->poses[i], record->poses[i]);
        if (!isfinite(length) || fabsf(length - 1.0f) > 0.01f) return false;
        for (unsigned j = 0; j < i; j++)
            if ((record->mask & (1u << j)) &&
                pose_dot(record->poses[i], record->poses[j]) > 0.85f) return false;
    }
    pose_init(tracker); // Never restore samples, a pending action, or countdown.
    for (unsigned i = 0; i < 5; i++) {
        tracker->taught[i] = (record->mask & (1u << i)) != 0;
        tracker->poses[i] = record->poses[i];
    }
    *enabled = record->enabled != 0;
    return true;
}
