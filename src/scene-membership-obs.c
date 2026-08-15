#include "scene-membership-obs.h"
#include "scene-membership.h"
#include <obs-frontend-api.h>

/* scene-membership.h's sm_node_t is an opaque void*, but this adapter has to bridge two real
 * libobs types through that one channel: a top-level scene (obs_scene_t*) and a scene item
 * (obs_sceneitem_t*, which may itself be a group). sm_scene_contains_source() calls
 * enum_children() on the root once, then again on any item for which item_is_group() returned
 * true - so enum_children() must be able to tell those two cases apart. Real libobs needs a
 * different call for each: obs_scene_enum_items() takes an obs_scene_t* directly for the root,
 * while a nested group's obs_scene_t* has to be fetched first via
 * obs_sceneitem_group_get_scene(). Tagging every node lets enum_children() pick the right one. */
typedef enum { OBS_NODE_SCENE, OBS_NODE_ITEM } obs_node_kind_t;

typedef struct {
    obs_node_kind_t kind;
    union {
        obs_scene_t *scene;    // valid when kind == OBS_NODE_SCENE
        obs_sceneitem_t *item; // valid when kind == OBS_NODE_ITEM
    } as;
} obs_node_t;

typedef struct {
    bool (*visit)(sm_node_t, void *);
    void *visit_ctx;
} enum_trampoline_ctx_t;

// obs_scene_enum_items() requires a callback shaped bool(obs_scene_t*, obs_sceneitem_t*, void*),
// which doesn't match sm's bool(sm_node_t, void*). Bridge with a plain trampoline instead of
// casting function pointers across mismatched signatures (undefined behavior).
static bool enum_trampoline(obs_scene_t *scene, obs_sceneitem_t *item, void *param) {
    (void)scene;
    enum_trampoline_ctx_t *tc = (enum_trampoline_ctx_t *)param;
    obs_node_t child = { .kind = OBS_NODE_ITEM, .as.item = item };
    return tc->visit((sm_node_t)&child, tc->visit_ctx);
}

static void obs_enum_children(sm_node_t node, void *ctx, bool (*visit)(sm_node_t, void *), void *visit_ctx) {
    (void)ctx;
    const obs_node_t *n = (const obs_node_t *)node;
    obs_scene_t *scene = n->kind == OBS_NODE_SCENE ? n->as.scene : obs_sceneitem_group_get_scene(n->as.item);
    if (!scene)
        return;

    enum_trampoline_ctx_t tc = { .visit = visit, .visit_ctx = visit_ctx };
    obs_scene_enum_items(scene, enum_trampoline, &tc);
}

static sm_node_t obs_item_source(sm_node_t item) {
    const obs_node_t *n = (const obs_node_t *)item;
    return (sm_node_t)obs_sceneitem_get_source(n->as.item);
}

static bool obs_item_is_group(sm_node_t item) {
    const obs_node_t *n = (const obs_node_t *)item;
    return obs_sceneitem_is_group(n->as.item);
}

static bool standby_scene_contains_source(obs_source_t *scene_source, obs_source_t *target) {
    if (!scene_source)
        return false;

    obs_scene_t *scene = obs_scene_from_source(scene_source);
    obs_node_t root = { .kind = OBS_NODE_SCENE, .as.scene = scene };

    static const sm_adapter_t adapter = {
        .enum_children = obs_enum_children,
        .item_source = obs_item_source,
        .item_is_group = obs_item_is_group,
    };

    bool found = scene ? sm_scene_contains_source(&root, (sm_node_t)target, &adapter) : false;
    obs_source_release(scene_source);
    return found;
}

bool standby_is_source_in_program(obs_source_t *target) {
    return standby_scene_contains_source(obs_frontend_get_current_scene(), target);
}

bool standby_is_source_in_preview(obs_source_t *target) {
    return standby_scene_contains_source(obs_frontend_get_current_preview_scene(), target);
}
