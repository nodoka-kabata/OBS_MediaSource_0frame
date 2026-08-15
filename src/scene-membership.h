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
