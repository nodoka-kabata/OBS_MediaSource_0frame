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

    printf("test_standby_state: all assertions passed\n");
    return 0;
}
