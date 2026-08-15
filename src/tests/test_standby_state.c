#include "../standby-state.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    bool new_flag;

    // off -> off: no change
    assert(standby_next_action(false, false, &new_flag) == STANDBY_ACTION_NONE);
    assert(new_flag == false);

    // off -> on: cut-in, play only
    assert(standby_next_action(false, true, &new_flag) == STANDBY_ACTION_PLAY);
    assert(new_flag == true);

    // on -> off: cut-out, pause+reset
    assert(standby_next_action(true, false, &new_flag) == STANDBY_ACTION_PAUSE_RESET);
    assert(new_flag == false);

    // on -> on: no change
    assert(standby_next_action(true, true, &new_flag) == STANDBY_ACTION_NONE);
    assert(new_flag == true);

    // Studio mode preview-only: prepare and hold frame 0
    assert(standby_preview_action(true, false, true) == STANDBY_ACTION_PREPARE_STANDBY);

    // Preview standby must not interfere with Program playback
    assert(standby_preview_action(true, true, true) == STANDBY_ACTION_NONE);

    // Outside Studio Mode there is no separate preview bus
    assert(standby_preview_action(false, false, true) == STANDBY_ACTION_NONE);

    // A source absent from Preview needs no preparation
    assert(standby_preview_action(true, false, false) == STANDBY_ACTION_NONE);

    printf("test_standby_state: all assertions passed\n");
    return 0;
}
