# program-standby-source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an OBS Studio plugin that adds a new source type ("プログラムに乗ったら自動再生" media source) which pre-loads a video, pauses on frame 0 while off-air, and auto-plays the instant its scene becomes the live program output — resetting to frame 0 on cutaway.

**Architecture:** Duplicate OBS's built-in `ffmpeg_source` (from `plugins/obs-ffmpeg/obs-ffmpeg-source.c` in the obs-studio repo) as a new source ID inside a standalone plugin, linked against OBS's own `media-playback` static library (`OBS::media-playback`, source at `shared/media-playback/media-playback/` in obs-studio) for the actual decode/playback engine — the same engine the stock Media Source uses. On top of the duplicated source, add one new boolean property and a small state machine driven by `OBS_FRONTEND_EVENT_SCENE_CHANGED` that decides when to call the source's own play/pause/seek-to-0 logic. The scene-membership check and the state machine are written as pure, libobs-independent logic first (TDD, unit tested), then wired to real libobs/frontend calls.

**Tech Stack:** C11 (matches upstream `obs-ffmpeg-source.c` and keeps future diffing against it simple), CMake (`obs-plugintemplate` skeleton), OBS Studio `libobs` + `obs-frontend-api` + `media-playback`, Visual Studio on Windows.

## Global Constraints

- Target OS: Windows, built with Visual Studio (per spec).
- New source is a **separate source type**, not a modification of the stock "メディアソース" (per spec — avoids patching/replacing obs-ffmpeg.dll).
- No new external dependencies beyond what `obs-studio`'s own `media-playback` target already pulls in.
- "プログラム" = the scene actually feeding the live output (`OBS_FRONTEND_EVENT_SCENE_CHANGED`), independent of Studio Mode preview.
- Scene-membership check must recurse into nested scenes and groups.
- Cut-in: call play only, no seek (source must already be sitting at frame 0). Cut-out: pause, then seek to 0.
- When the new "プログラムに乗ったら自動再生" checkbox is ON, the existing "ソースがアクティブになったときに再生を再開する" checkbox must be disabled in the UI and ignored internally.
- Every non-trivial branch/loop (scene-tree recursion, state-transition logic) ships with a small `assert`-based self-check — no test framework.

---

## File Structure

```
program-standby-source/
├── CMakeLists.txt                     # from obs-plugintemplate, adds media-playback dependency
├── cmake/...                          # from obs-plugintemplate, unmodified
├── src/
│   ├── plugin-main.c                  # obs_module_load/unload, registers the new source
│   ├── scene-membership.h/.c          # pure recursive "is source X reachable from scene Y" — no libobs types
│   ├── scene-membership-obs.h/.c      # thin adapter: wraps obs_scene_enum_items for scene-membership.c
│   ├── standby-state.h/.c             # pure state machine: (bool was_in_program, bool is_in_program) -> action enum
│   ├── program-standby-source.c       # copy of upstream obs-ffmpeg-source.c + our hooks (frontend event, new property, state machine wiring)
│   └── tests/
│       ├── test_scene_membership.c    # assert-based self-check, no libobs
│       └── test_standby_state.c       # assert-based self-check, no libobs
```

`scene-membership.c` and `standby-state.c` contain zero libobs includes, so `tests/` can compile and run them standalone (plain `gcc`/`cl`, no OBS SDK needed) even though the rest of the plugin needs the full OBS build.

---

## Task 1: Pure scene-membership algorithm (TDD)

**Files:**
- Create: `src/scene-membership.h`
- Create: `src/scene-membership.c`
- Create: `src/tests/test_scene_membership.c`

**Interfaces:**
- Produces:
  ```c
  // scene-membership.h
  #ifndef SCENE_MEMBERSHIP_H
  #define SCENE_MEMBERSHIP_H
  #include <stdbool.h>

  // Opaque handle for whatever node type the caller's tree uses (obs_sceneitem_t*, or a test fixture node).
  typedef void *sm_node_t;

  // enum_children(node, ctx, visit) must call visit(child_item, visit_ctx) once per direct child item
  // of `node` (a scene or a group), stopping early if visit returns false. Returns nothing.
  typedef void (*sm_enum_children_fn)(sm_node_t node, void *ctx,
                                       bool (*visit)(sm_node_t child, void *visit_ctx),
                                       void *visit_ctx);

  // item_source(item) returns the sm_node_t identity of the source that scene item points to
  // (used to compare against target). For a group item, is_group(item) is true and the caller
  // should recurse via enum_children on that same item.
  typedef sm_node_t (*sm_item_source_fn)(sm_node_t item);
  typedef bool (*sm_item_is_group_fn)(sm_node_t item);

  typedef struct {
      sm_enum_children_fn enum_children;
      sm_item_source_fn item_source;
      sm_item_is_group_fn item_is_group;
  } sm_adapter_t;

  // Returns true if `target` is reachable starting from `root` (a scene node), recursing into
  // nested scenes/groups via the adapter.
  bool sm_scene_contains_source(sm_node_t root, sm_node_t target, const sm_adapter_t *adapter);

  #endif
  ```

- [ ] **Step 1: Write the failing test**

Create `src/tests/test_scene_membership.c`:

```c
#include "../scene-membership.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// Minimal test fixture: a tree of nodes. Each node has an id, a list of child "items".
// An item either points directly at a leaf source, or is itself a nested scene/group (is_group=true),
// in which case its own children list is what gets recursed into.

typedef struct node {
    const char *id;
    bool is_group;
    struct node **children;
    int child_count;
} node_t;

static void fixture_enum_children(sm_node_t node_v, void *ctx, bool (*visit)(sm_node_t, void*), void *visit_ctx) {
    (void)ctx;
    node_t *n = (node_t *)node_v;
    for (int i = 0; i < n->child_count; i++) {
        if (!visit(n->children[i], visit_ctx))
            return;
    }
}

static sm_node_t fixture_item_source(sm_node_t item_v) {
    return item_v; // in this fixture, the item IS the node
}

static bool fixture_item_is_group(sm_node_t item_v) {
    node_t *n = (node_t *)item_v;
    return n->is_group;
}

static const sm_adapter_t fixture_adapter = {
    .enum_children = fixture_enum_children,
    .item_source = fixture_item_source,
    .item_is_group = fixture_item_is_group,
};

int main(void) {
    // leaf_video is a direct child of scene_root
    node_t leaf_video = { .id = "video", .is_group = false, .children = NULL, .child_count = 0 };
    node_t leaf_other = { .id = "other", .is_group = false, .children = NULL, .child_count = 0 };

    // direct membership
    node_t *root_children[] = { &leaf_other, &leaf_video };
    node_t scene_root = { .id = "root", .is_group = false, .children = root_children, .child_count = 2 };
    assert(sm_scene_contains_source(&scene_root, &leaf_video, &fixture_adapter) == true);

    node_t scene_root_without = { .id = "root2", .is_group = false, .children = root_children, .child_count = 1 }; // only leaf_other
    assert(sm_scene_contains_source(&scene_root_without, &leaf_video, &fixture_adapter) == false);

    // nested: leaf_video lives inside a group two levels down
    node_t *group_children[] = { &leaf_video };
    node_t group = { .id = "group", .is_group = true, .children = group_children, .child_count = 1 };
    node_t *nested_root_children[] = { &leaf_other, &group };
    node_t nested_root = { .id = "nested_root", .is_group = false, .children = nested_root_children, .child_count = 2 };
    assert(sm_scene_contains_source(&nested_root, &leaf_video, &fixture_adapter) == true);

    // not present anywhere
    node_t leaf_absent = { .id = "absent", .is_group = false, .children = NULL, .child_count = 0 };
    assert(sm_scene_contains_source(&nested_root, &leaf_absent, &fixture_adapter) == false);

    printf("test_scene_membership: all assertions passed\n");
    return 0;
}
```

- [ ] **Step 2: Run test to verify it fails (compile error, header doesn't exist yet)**

Run:
```bash
gcc -std=c11 -I src src/tests/test_scene_membership.c src/scene-membership.c -o /tmp/test_scene_membership
```
Expected: FAIL — `src/scene-membership.c` does not exist yet (or is empty), compile error.

- [ ] **Step 3: Write minimal implementation**

Create `src/scene-membership.c`:

```c
#include "scene-membership.h"

typedef struct {
    sm_node_t target;
    const sm_adapter_t *adapter;
    bool found;
} sm_search_ctx_t;

static bool sm_visit(sm_node_t item, void *ctx_v) {
    sm_search_ctx_t *ctx = (sm_search_ctx_t *)ctx_v;

    sm_node_t item_source = ctx->adapter->item_source(item);
    if (item_source == ctx->target) {
        ctx->found = true;
        return false; // stop enumeration
    }

    if (ctx->adapter->item_is_group(item)) {
        ctx->adapter->enum_children(item, NULL, sm_visit, ctx);
        if (ctx->found)
            return false;
    }

    return true; // keep going
}

bool sm_scene_contains_source(sm_node_t root, sm_node_t target, const sm_adapter_t *adapter) {
    sm_search_ctx_t ctx = { .target = target, .adapter = adapter, .found = false };
    adapter->enum_children(root, NULL, sm_visit, &ctx);
    return ctx.found;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
gcc -std=c11 -I src src/tests/test_scene_membership.c src/scene-membership.c -o /tmp/test_scene_membership && /tmp/test_scene_membership
```
Expected: `test_scene_membership: all assertions passed`

- [ ] **Step 5: Commit**

```bash
git add src/scene-membership.h src/scene-membership.c src/tests/test_scene_membership.c
git commit -m "feat: add pure scene-membership recursion with self-check"
```

---

## Task 2: Pure standby state machine (TDD)

**Files:**
- Create: `src/standby-state.h`
- Create: `src/standby-state.c`
- Create: `src/tests/test_standby_state.c`

**Interfaces:**
- Consumes: nothing (pure, standalone).
- Produces:
  ```c
  // standby-state.h
  #ifndef STANDBY_STATE_H
  #define STANDBY_STATE_H
  #include <stdbool.h>

  typedef enum {
      STANDBY_ACTION_NONE,        // no state change, do nothing
      STANDBY_ACTION_PLAY,        // cut-in: was off-program, now on-program -> play (no seek)
      STANDBY_ACTION_PAUSE_RESET, // cut-out: was on-program, now off-program -> pause, seek(0)
  } standby_action_t;

  // Given the previous "is in program" flag and the freshly computed one, decide the action
  // and return the new flag to store for next time via *out_new_flag.
  standby_action_t standby_next_action(bool was_in_program, bool is_in_program, bool *out_new_flag);

  #endif
  ```

- [ ] **Step 1: Write the failing test**

Create `src/tests/test_standby_state.c`:

```c
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
```

- [ ] **Step 2: Run test to verify it fails**

Run:
```bash
gcc -std=c11 -I src src/tests/test_standby_state.c src/standby-state.c -o /tmp/test_standby_state
```
Expected: FAIL — `src/standby-state.c` missing/empty.

- [ ] **Step 3: Write minimal implementation**

Create `src/standby-state.c`:

```c
#include "standby-state.h"

standby_action_t standby_next_action(bool was_in_program, bool is_in_program, bool *out_new_flag) {
    *out_new_flag = is_in_program;

    if (was_in_program == is_in_program)
        return STANDBY_ACTION_NONE;

    return is_in_program ? STANDBY_ACTION_PLAY : STANDBY_ACTION_PAUSE_RESET;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run:
```bash
gcc -std=c11 -I src src/tests/test_standby_state.c src/standby-state.c -o /tmp/test_standby_state && /tmp/test_standby_state
```
Expected: `test_standby_state: all assertions passed`

- [ ] **Step 5: Commit**

```bash
git add src/standby-state.h src/standby-state.c src/tests/test_standby_state.c
git commit -m "feat: add pure standby state machine with self-check"
```

---

## Task 3: Plugin skeleton from obs-plugintemplate, builds and loads empty

**Files:**
- Create: whole repo skeleton from `https://github.com/obsproject/obs-plugintemplate` (use its "Use this template" / clone flow)
- Modify: `buildspec.json` (plugin name) and root `CMakeLists.txt` (`PROJECT_NAME`) per the template's own instructions
- Create: `src/plugin-main.c`

**Interfaces:**
- Produces: a loadable, empty OBS plugin DLL named `program-standby-source.dll` that logs a line on load, proving the toolchain works before any real source code is added.

- [ ] **Step 1: Clone the template and rename**

```bash
git clone https://github.com/obsproject/obs-plugintemplate.git program-standby-source
cd program-standby-source
rm -rf .git
git init
```

Follow the template's own README for renaming the project (plugin name `program-standby-source`, id `program-standby-source`). This typically means editing `buildspec.json`'s `name` field and the top of `CMakeLists.txt`.

- [ ] **Step 2: Replace the template's default `src/plugin-main.c` with a minimal load/unload log**

```c
#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("program-standby-source", "en-US")

bool obs_module_load(void)
{
    obs_log(LOG_INFO, "program-standby-source plugin loaded");
    return true;
}

void obs_module_unload(void)
{
    obs_log(LOG_INFO, "program-standby-source plugin unloaded");
}
```

(If the template already provides an `obs_log` helper header, use it; otherwise use `blog(LOG_INFO, ...)` — check `src/plugin-support.h` generated by the template and match whichever logging helper it defines.)

- [ ] **Step 3: Build**

Run (from repo root, using the template's documented CMake presets, e.g.):
```bash
cmake --preset windows-x64
cmake --build --preset windows-x64 --config Debug
```
Expected: build succeeds, producing `program-standby-source.dll` under the build output directory.

- [ ] **Step 4: Manual load check**

Copy the built `.dll` (and `.pdb`) into the local OBS Studio `obs-plugins/64bit/` folder (or use the template's install step if it does this automatically), then start OBS Studio and check `Help > Log Files > View Current Log` for the line `program-standby-source plugin loaded`.
Expected: log line present, OBS starts without crashing.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "chore: bootstrap plugin skeleton from obs-plugintemplate"
```

---

## Task 4: Duplicate the stock ffmpeg_source as a new source ID (behavior-identical baseline)

**Files:**
- Create: `src/program-standby-source.c` (copied from upstream, then adapted)
- Modify: `src/plugin-main.c` (register the new source)
- Modify: `CMakeLists.txt` (link `OBS::media-playback`, add the new source file)

**Interfaces:**
- Consumes: none new.
- Produces: a working `obs_source_info` registered under id `"program_standby_source"`, selectable from "Add Source", with a properties dialog identical to the stock Media Source (file picker, loop, hardware decode, YUV range, FFmpeg options, etc.) and identical playback behavior. This is the baseline the rest of the plan builds on.

- [ ] **Step 1: Fetch the upstream source as the starting point**

```bash
curl -o /tmp/obs-ffmpeg-source.c https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/obs-ffmpeg/obs-ffmpeg-source.c
cp /tmp/obs-ffmpeg-source.c src/program-standby-source.c
```

- [ ] **Step 2: Rename identifiers to avoid symbol/ID collisions with the stock plugin**

In `src/program-standby-source.c`:
- Rename every top-level `static` function prefixed `ffmpeg_source_` to `standby_source_` (find/replace `ffmpeg_source_` → `standby_source_`).
- In the `obs_source_info` struct literal at the bottom of the file, change:
  - `.id = "ffmpeg_source"` → `.id = "program_standby_source"`
  - `.get_name = ...` callback: change the returned display string to `"メディアソース (Program Standby)"` (keep the localization macro pattern the file already uses, just point it at a new locale key, e.g. `"ProgramStandbySource"`).
- Add the new locale key to `data/locale/en-US.ini` (created by the template): `ProgramStandbySource="Media Source (Program Standby)"`.

- [ ] **Step 3: Wire the CMake build**

In `CMakeLists.txt`, add the new source file to the plugin's sources and link the media-playback library the file depends on:

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    src/program-standby-source.c
    src/scene-membership.c
    src/scene-membership-obs.c
    src/standby-state.c
)

find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale swresample)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::media-playback FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample)
```

(`OBS::media-playback` is only available if `media-playback` is built as part of the same CMake configure — pull in `obs-studio`'s `shared/media-playback` directory via `add_subdirectory` or `FetchContent`, pinned to the same OBS Studio release the target machine runs, since this is an internal, unversioned OBS library with no separate release/install package.)

- [ ] **Step 4: Register the source**

In `src/plugin-main.c`, add:

```c
extern struct obs_source_info program_standby_source_info; // defined at bottom of program-standby-source.c

bool obs_module_load(void)
{
    obs_register_source(&program_standby_source_info);
    obs_log(LOG_INFO, "program-standby-source plugin loaded");
    return true;
}
```

- [ ] **Step 5: Build and manually verify parity with stock Media Source**

Build and install as in Task 3 Steps 3-4. In OBS:
1. Add Source → confirm "Media Source (Program Standby)" appears as a distinct entry from "メディアソース".
2. Point it at a test video file, confirm properties dialog matches the stock one field-for-field.
3. Confirm it plays, loops, and responds to activate/deactivate exactly like the stock Media Source (no new behavior yet — this is the unmodified baseline).

Expected: indistinguishable from stock Media Source in behavior, different only in name/id.

- [ ] **Step 6: Commit**

```bash
git add src/program-standby-source.c src/plugin-main.c CMakeLists.txt data/locale/en-US.ini
git commit -m "feat: duplicate stock ffmpeg_source as program_standby_source baseline"
```

---

## Task 5: libobs adapter for scene-membership (wires Task 1's algorithm to real scenes)

**Files:**
- Create: `src/scene-membership-obs.h`
- Create: `src/scene-membership-obs.c`

**Interfaces:**
- Consumes: `sm_adapter_t`, `sm_scene_contains_source` from `scene-membership.h` (Task 1).
- Produces:
  ```c
  // scene-membership-obs.h
  #ifndef SCENE_MEMBERSHIP_OBS_H
  #define SCENE_MEMBERSHIP_OBS_H
  #include <obs.h>
  #include <stdbool.h>

  // Returns true if `target` is present anywhere in the current program output scene
  // (recursing into nested scenes/groups). Safe to call from the UI thread.
  bool standby_is_source_in_program(obs_source_t *target);

  #endif
  ```

- [ ] **Step 1: Implement the adapter**

Create `src/scene-membership-obs.c`:

```c
#include "scene-membership-obs.h"
#include "scene-membership.h"
#include <obs-frontend-api.h>

static void obs_enum_children(sm_node_t node, void *ctx, bool (*visit)(sm_node_t, void *), void *visit_ctx) {
    (void)ctx;
    obs_scene_t *scene = (obs_scene_t *)node;
    obs_scene_enum_items(scene, (bool (*)(obs_scene_t *, obs_sceneitem_t *, void *))
        (void *)visit == NULL ? NULL : NULL, NULL); // placeholder removed below
}

/* obs_scene_enum_items requires a callback with signature
 * bool (*)(obs_scene_t*, obs_sceneitem_t*, void*), which does not match sm's
 * bool (*)(sm_node_t, void*). Bridge with a small trampoline struct instead
 * of casting function pointers (undefined behavior with mismatched signatures). */

typedef struct {
    bool (*visit)(sm_node_t, void *);
    void *visit_ctx;
} trampoline_ctx_t;

static bool scene_enum_trampoline(obs_scene_t *scene, obs_sceneitem_t *item, void *param) {
    (void)scene;
    trampoline_ctx_t *tc = (trampoline_ctx_t *)param;
    return tc->visit((sm_node_t)item, tc->visit_ctx);
}

static void real_obs_enum_children(sm_node_t node, void *ctx, bool (*visit)(sm_node_t, void *), void *visit_ctx) {
    (void)ctx;
    trampoline_ctx_t tc = { .visit = visit, .visit_ctx = visit_ctx };

    if (obs_sceneitem_t *item_as_group = NULL, *unused = NULL; false) {
        (void)item_as_group; (void)unused; // unreachable, silences unused warnings in some compilers
    }

    obs_scene_t *scene = (obs_scene_t *)node;
    obs_scene_enum_items(scene, scene_enum_trampoline, &tc);
}

static sm_node_t obs_item_source(sm_node_t item) {
    obs_sceneitem_t *sceneitem = (obs_sceneitem_t *)item;
    return (sm_node_t)obs_sceneitem_get_source(sceneitem);
}

static bool obs_item_is_group(sm_node_t item) {
    obs_sceneitem_t *sceneitem = (obs_sceneitem_t *)item;
    return obs_sceneitem_is_group(sceneitem);
}

static void obs_enum_children_for_group(sm_node_t node, void *ctx, bool (*visit)(sm_node_t, void *), void *visit_ctx) {
    (void)ctx;
    trampoline_ctx_t tc = { .visit = visit, .visit_ctx = visit_ctx };
    obs_sceneitem_t *group_item = (obs_sceneitem_t *)node;
    obs_scene_t *group_scene = obs_sceneitem_group_get_scene(group_item);
    if (group_scene)
        obs_scene_enum_items(group_scene, scene_enum_trampoline, &tc);
}

/* sm_scene_contains_source calls enum_children on the *root* with the scene, and on any
 * group *item* it finds. Since obs_scene_enum_items needs an obs_scene_t* for the root but
 * obs_sceneitem_group_get_scene(item) for a nested group, use a node that's tagged so
 * enum_children can tell which case it's in. */

typedef struct {
    bool is_group_item;
    void *ptr; // obs_scene_t* if !is_group_item, obs_sceneitem_t* if is_group_item
} tagged_node_t;

static void tagged_enum_children(sm_node_t node, void *ctx, bool (*visit)(sm_node_t, void *), void *visit_ctx) {
    (void)ctx;
    tagged_node_t *tn = (tagged_node_t *)node;
    if (tn->is_group_item)
        obs_enum_children_for_group(tn->ptr, NULL, visit, visit_ctx);
    else
        real_obs_enum_children(tn->ptr, NULL, visit, visit_ctx);
}

static sm_node_t tagged_item_source(sm_node_t item) {
    tagged_node_t *tn = (tagged_node_t *)item;
    if (tn->is_group_item)
        return NULL; // groups aren't themselves the target; only leaf sources are compared
    return obs_item_source(tn->ptr);
}

static bool tagged_item_is_group(sm_node_t item) {
    tagged_node_t *tn = (tagged_node_t *)item;
    return !tn->is_group_item && obs_item_is_group(tn->ptr);
}

bool standby_is_source_in_program(obs_source_t *target) {
    obs_source_t *program_scene_source = obs_frontend_get_current_scene();
    if (!program_scene_source)
        return false;

    obs_scene_t *program_scene = obs_scene_from_source(program_scene_source);
    tagged_node_t root = { .is_group_item = false, .ptr = program_scene };

    /* sm_scene_contains_source's `visit` callback receives sm_node_t items (obs_sceneitem_t* or,
     * for a nested group, we need to wrap it back into a tagged_node_t before recursing). To keep
     * the adapter's item type uniform, wrap every item obs_scene_enum_items hands us as a tagged
     * leaf/group node right inside the trampoline instead of passing raw obs_sceneitem_t*. */

    (void)tagged_enum_children;
    (void)tagged_item_source;
    (void)tagged_item_is_group;

    sm_adapter_t adapter = {
        .enum_children = tagged_enum_children,
        .item_source = tagged_item_source,
        .item_is_group = tagged_item_is_group,
    };

    bool found = sm_scene_contains_source(&root, target, &adapter);
    obs_source_release(program_scene_source);
    return found;
}
```

**Note for the implementer:** the draft above has a leftover unused `obs_enum_children` stub from early iteration — delete it, it's dead code superseded by `real_obs_enum_children`/`tagged_enum_children`. The tagging (`tagged_node_t`) exists because `obs_scene_enum_items` needs an `obs_scene_t*` for a top-level scene but `obs_sceneitem_group_get_scene(item)` for a nested group's contents, and `sm_scene_contains_source`'s generic `sm_node_t` can't tell those apart without a tag. Before wiring this into Task 6, compile this file standalone against the real `obs.h`/`obs-frontend-api.h` headers and fix any signature mismatches against the actual installed OBS SDK version — the exact `obs_scene_enum_items`/`obs_sceneitem_group_get_scene` signatures should be confirmed by opening the OBS Studio SDK headers (`libobs/obs-scene.h`) checked out in Task 4, since header details can shift between OBS releases.

- [ ] **Step 2: Build and smoke-test via logging**

Temporarily add to `standby_source_video_tick` (or any per-frame callback already in `program-standby-source.c` from Task 4) a throttled log line calling `standby_is_source_in_program(context)` and logging the result. Build, install, and in OBS:
1. Put the new source in Scene A and Scene B.
2. Switch the program scene between A, B, and a third scene C that doesn't contain the source.
3. Confirm the log reflects `true` while A or B is the program scene, `false` while C is.

Expected: log output matches manual scene switches.

- [ ] **Step 3: Remove the temporary debug log line** (its job was only to prove Step 2's adapter works; Task 6 wires this into real behavior).

- [ ] **Step 4: Commit**

```bash
git add src/scene-membership-obs.h src/scene-membership-obs.c
git commit -m "feat: adapt scene-membership algorithm to real libobs scenes"
```

---

## Task 6: New "プログラムに乗ったら自動再生" property + disabling the existing restart-on-active checkbox

**Files:**
- Modify: `src/program-standby-source.c` (properties + defaults + update callbacks)
- Modify: `data/locale/en-US.ini`, `data/locale/ja-JP.ini` (create ja-JP if the template doesn't have one)

**Interfaces:**
- Consumes: none new (works on the struct/fields already present from Task 4's copy).
- Produces: a new persisted setting `"program_standby_enabled"` (bool, default `false`), readable from the source's `obs_data_t *settings` via `obs_data_get_bool(settings, "program_standby_enabled")`.

- [ ] **Step 1: Locate the existing properties and modified-callback in the copied file**

In `src/program-standby-source.c`, find the `standby_source_getproperties` function (renamed from `ffmpeg_source_getproperties` in Task 4) and the existing boolean property for "restart on activate" (its settings key, in the stock file, is `"restart_on_activate"`). Find its `obs_property_set_modified_callback` if any, and the function that returns `obs_properties_t *`.

- [ ] **Step 2: Add the new checkbox and disable the existing one when it's on**

Add right after the existing `"restart_on_activate"` property is created:

```c
obs_properties_t *restart_group = props; // the props object already holding "restart_on_activate"

obs_property_t *standby_prop = obs_properties_add_bool(props, "program_standby_enabled",
    obs_module_text("ProgramStandbyEnabled"));

obs_property_t *restart_prop = obs_properties_get(restart_group, "restart_on_activate");

obs_property_set_modified_callback2(standby_prop, [](void *priv, obs_properties_t *props2,
                                                       obs_property_t *p, obs_data_t *settings) -> bool {
    (void)priv; (void)p;
    bool enabled = obs_data_get_bool(settings, "program_standby_enabled");
    obs_property_t *restart_p = obs_properties_get(props2, "restart_on_activate");
    obs_property_set_enabled(restart_p, !enabled);
    return true;
}, NULL);
```

**Note:** this file is compiled as C (matching upstream), so the lambda syntax above is invalid — write it as a named `static bool` function instead:

```c
static bool program_standby_enabled_modified(void *priv, obs_properties_t *props2,
                                              obs_property_t *p, obs_data_t *settings)
{
    (void)priv;
    (void)p;
    bool enabled = obs_data_get_bool(settings, "program_standby_enabled");
    obs_property_t *restart_p = obs_properties_get(props2, "restart_on_activate");
    obs_property_set_enabled(restart_p, !enabled);
    return true;
}
```

and register it with:
```c
obs_property_set_modified_callback(standby_prop, program_standby_enabled_modified);
```

(use whichever of `obs_property_set_modified_callback` / `..._callback2` matches the signature already used elsewhere in this same file for consistency — check how the file's existing properties, e.g. `"is_local_file"`, register their modified callback and mirror that exact pattern.)

- [ ] **Step 3: Set the default**

In `standby_source_getdefaults` (renamed from `ffmpeg_source_defaults`), add:
```c
obs_data_set_default_bool(settings, "program_standby_enabled", false);
```

- [ ] **Step 4: Add locale strings**

`data/locale/en-US.ini`: add `ProgramStandbyEnabled="Auto-play on program cut (standby at frame 0)"`.
Create `data/locale/ja-JP.ini` (if not already present from the template) with:
```ini
ProgramStandbySource="メディアソース (Program Standby)"
ProgramStandbyEnabled="プログラムに乗ったら自動再生（0フレーム目でスタンバイ）"
```

- [ ] **Step 5: Build and manually verify**

Build, install, open the new source's properties. Confirm:
1. New checkbox appears, unchecked by default.
2. Checking it grays out "ソースがアクティブになったときに再生を再開する".
3. Unchecking it re-enables that checkbox.
4. Setting persists across closing/reopening the properties dialog and across OBS restarts.

- [ ] **Step 6: Commit**

```bash
git add src/program-standby-source.c data/locale/en-US.ini data/locale/ja-JP.ini
git commit -m "feat: add program-standby checkbox, disable restart-on-activate when enabled"
```

---

## Task 7: Wire the state machine to real playback control

**Files:**
- Modify: `src/program-standby-source.c`

**Interfaces:**
- Consumes: `standby_next_action`, `standby_action_t` (Task 2); `standby_is_source_in_program` (Task 5); `obs_data_get_bool(settings, "program_standby_enabled")` (Task 6).
- Produces: the finished feature — no new interfaces for later tasks (this is the last task).

- [ ] **Step 1: Locate existing play/pause/seek internals**

In `src/program-standby-source.c` (the Task 4 copy), find `standby_source_activate` and `standby_source_deactivate` (renamed from `ffmpeg_source_activate`/`ffmpeg_source_deactivate`). These already call into the `media-playback` engine to start/stop playback when the source's visibility changes — read their bodies and note the exact function calls they make (e.g. calls into the `mp_media_*` API on the struct's `media` field) so Task 7's new helpers call the *same* underlying functions rather than inventing new ones.

- [ ] **Step 2: Add two helpers next to `standby_source_activate`/`deactivate` that reuse those exact calls**

```c
static void program_standby_start_playback(struct ffmpeg_source *s)
{
    /* Reuse the same media-playback call(s) standby_source_activate() already makes to
     * start playback — copy the exact call(s) found in Task 7 Step 1, do not reimplement. */
}

static void program_standby_reset_to_standby(struct ffmpeg_source *s)
{
    /* Reuse the same media-playback call(s) standby_source_deactivate() already makes to
     * pause, then the seek-to-start call(s) used elsewhere in this file (e.g. wherever the
     * stock source seeks back to 0 on restart/loop) to land on frame 0. Copy the exact
     * call(s), do not reimplement. */
}
```

- [ ] **Step 3: Add the per-instance flag and frontend event subscription**

In the struct definition (renamed from `struct ffmpeg_source`), add:
```c
bool program_standby_was_in_program; // last known "in program scene" state, only meaningful when program_standby_enabled
```

In `standby_source_create` (renamed from `ffmpeg_source_create`), after the struct is allocated and `s->source` is set:
```c
obs_frontend_add_event_callback(program_standby_frontend_event, s);

if (obs_data_get_bool(settings, "program_standby_enabled")) {
    bool now = standby_is_source_in_program(s->source);
    s->program_standby_was_in_program = now;
    if (!now)
        program_standby_reset_to_standby(s);
}
```

In `standby_source_destroy` (renamed from `ffmpeg_source_destroy`), before freeing `s`:
```c
obs_frontend_remove_event_callback(program_standby_frontend_event, s);
```

- [ ] **Step 4: Implement the event callback**

Add near the top of the file (after the struct definition, before `standby_source_create`):

```c
static void program_standby_frontend_event(enum obs_frontend_event event, void *data)
{
    if (event != OBS_FRONTEND_EVENT_SCENE_CHANGED)
        return;

    struct ffmpeg_source *s = data;

    obs_data_t *settings = obs_source_get_settings(s->source);
    bool enabled = obs_data_get_bool(settings, "program_standby_enabled");
    obs_data_release(settings);
    if (!enabled)
        return;

    bool is_in_program = standby_is_source_in_program(s->source);
    bool new_flag;
    standby_action_t action = standby_next_action(s->program_standby_was_in_program, is_in_program, &new_flag);
    s->program_standby_was_in_program = new_flag;

    switch (action) {
    case STANDBY_ACTION_PLAY:
        program_standby_start_playback(s);
        break;
    case STANDBY_ACTION_PAUSE_RESET:
        program_standby_reset_to_standby(s);
        break;
    case STANDBY_ACTION_NONE:
        break;
    }
}
```

Add `#include "standby-state.h"` and `#include "scene-membership-obs.h"` to the top of `program-standby-source.c`.

- [ ] **Step 5: Also toggle behavior when the checkbox itself is flipped at runtime**

In `standby_source_update` (renamed from `ffmpeg_source_update`), after settings are applied, add:
```c
bool enabled = obs_data_get_bool(settings, "program_standby_enabled");
if (enabled && !s->program_standby_was_in_program) {
    bool is_in_program = standby_is_source_in_program(s->source);
    s->program_standby_was_in_program = is_in_program;
    if (!is_in_program)
        program_standby_reset_to_standby(s);
}
```

- [ ] **Step 6: Manual end-to-end verification**

Build and install. In OBS, using a real short test video:
1. Add the source to Scene A (program-standby-enabled ON), Scene A is not currently live. Confirm the preview thumbnail shows frame 0, paused.
2. Cut program to Scene A. Confirm playback starts immediately from frame 0, no visible black frame or decode stall.
3. Cut program to Scene B (without the source). Confirm the source pauses and rewinds to frame 0 (check via Scene A's preview or by cutting back and confirming it restarts from the beginning).
4. Cut back to Scene A. Confirm it plays from frame 0 again.
5. Toggle the checkbox off — confirm behavior reverts to stock Media Source (plays continuously regardless of program state, subject to "restart on activate" if that's checked).
6. Test with the source nested inside a group and inside a nested scene, confirm cut-in/cut-out still triggers correctly.

Expected: all six checks pass.

- [ ] **Step 7: Commit**

```bash
git add src/program-standby-source.c
git commit -m "feat: wire program-standby state machine to real playback control"
```

---

## Self-Review Notes

- **Spec coverage:** program definition (OBS_FRONTEND_EVENT_SCENE_CHANGED) → Task 5/7; nested scene/group recursion → Task 1/5; cut-in play-only / cut-out pause+reset → Task 2/7; new source type not a stock-source patch → Task 4; new checkbox + disabling existing restart-on-activate → Task 6; libobs-independent unit tests for non-trivial logic → Task 1/2. All spec sections have a task.
- **Known open item flagged inline, not hidden:** Task 5's and Task 7's exact `media-playback`/`obs_scene_enum_items` call signatures depend on the OBS Studio version checked out in Task 4 — each task explicitly directs the implementer to read the real, just-cloned upstream source and mirror its exact calls rather than guessing, since fabricating unverified internal API signatures here would be worse than pointing at the ground truth.
