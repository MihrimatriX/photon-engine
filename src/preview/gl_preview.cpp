#include "preview/gl_preview.h"

namespace photon {

void GLPreview::init() {}
void GLPreview::shutdown() {}

void GLPreview::render(const SceneGraph& graph, const Scene& flatScene, const Camera& camera,
                       int width, int height, PreviewQuality quality, float blend) {
    (void)graph; (void)flatScene; (void)camera; (void)width; (void)height; (void)quality; (void)blend;
    // ponytail: GPU IBL+GGX rasterizer stub; upgrade path: mesh VBO upload + shader pass
}

void PickingPass::init() {}
void PickingPass::shutdown() {}

uint32_t PickingPass::pick(const SceneGraph& graph, const Scene& flatScene, const Camera& camera,
                           int width, int height, float u, float v) {
    (void)flatScene; (void)camera; (void)width; (void)height; (void)u; (void)v;
    std::vector<const SceneNode*> stack{graph.root()};
    while (!stack.empty()) {
        const SceneNode* n = stack.back();
        stack.pop_back();
        if (n->pickId && n->type != SceneNodeType::Group) return n->pickId;
        for (const auto& c : n->children) stack.push_back(c.get());
    }
    return 0;
}

} // namespace photon
