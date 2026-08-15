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
    assert(standby_studio_action(true, false, true, false) == STANDBY_ACTION_PREPARE_STANDBY);

    // Preview standby must not interfere with Program playback
    assert(standby_studio_action(true, true, true, false) == STANDBY_ACTION_NONE);

    // Outside Studio Mode there is no separate preview bus
    assert(standby_studio_action(false, false, true, false) == STANDBY_ACTION_NONE);

    // Leaving both Program and Preview returns an already-loaded source to frame 0
    assert(standby_studio_action(true, false, false, false) == STANDBY_ACTION_PAUSE_RESET);

    // If playback already ended, leaving both Program and Preview must restart decoding
    // before pausing at frame 0; a plain pause+seek cannot revive an ended decoder.
    assert(standby_studio_action(true, false, false, true) == STANDBY_ACTION_PREPARE_STANDBY);

    printf("test_standby_state: all assertions passed\n");
    return 0;
}
