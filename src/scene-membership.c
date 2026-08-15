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
