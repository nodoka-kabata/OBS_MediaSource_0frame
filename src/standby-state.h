#ifndef STANDBY_STATE_H
#define STANDBY_STATE_H
#include <stdbool.h>

typedef enum {
    STANDBY_ACTION_NONE,        // no state change, do nothing
    STANDBY_ACTION_PLAY,        // cut-in: was off-program, now on-program -> play (no seek)
    STANDBY_ACTION_PAUSE_RESET, // cut-out: was on-program, now off-program -> pause, seek(0)
    STANDBY_ACTION_PREPARE_STANDBY, // preview-only: load and hold frame 0
} standby_action_t;

// Given the previous "is in program" flag and the freshly computed one, decide the action
// and return the new flag to store for next time via *out_new_flag.
standby_action_t standby_next_action(bool was_in_program, bool is_in_program, bool *out_new_flag);

// Preview standby exists only on the separate Studio Mode preview bus and must never
// interfere with a source that is currently in Program.
standby_action_t standby_preview_action(bool studio_mode, bool is_in_program, bool is_in_preview);

#endif
