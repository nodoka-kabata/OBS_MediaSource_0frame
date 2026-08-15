#ifndef SCENE_MEMBERSHIP_OBS_H
#define SCENE_MEMBERSHIP_OBS_H
#include <obs.h>
#include <stdbool.h>

// Returns true if `target` is present anywhere in the current program output scene
// (recursing into nested scenes/groups). Safe to call from the UI thread.
bool standby_is_source_in_program(obs_source_t *target);

// Returns true if `target` is present anywhere in the current Studio Mode preview scene.
bool standby_is_source_in_preview(obs_source_t *target);

#endif
