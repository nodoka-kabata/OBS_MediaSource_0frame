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
