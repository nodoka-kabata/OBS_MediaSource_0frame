#include "standby-state.h"

standby_action_t standby_next_action(bool was_in_program, bool is_in_program, bool *out_new_flag) {
    *out_new_flag = is_in_program;

    if (was_in_program == is_in_program)
        return STANDBY_ACTION_NONE;

    return is_in_program ? STANDBY_ACTION_PLAY : STANDBY_ACTION_PAUSE_RESET;
}
