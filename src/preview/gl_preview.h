#pragma once

#include "scene/scene_graph.h"
#include "engine/scene.h"
#include "camera/camera.h"
#include <cstdint>

namespace photon {

enum class PreviewQuality { Fast, Quality };

class GLPreview {
public:
    void init();
    void shutdown();
    void render(const SceneGraph& graph, const Scene& flatScene, const Camera& camera,
                int width, int height, PreviewQuality quality, float blend = 1.0f);
    unsigned int colorTexture() const { return 0; }
};

class PickingPass {
public:
    void init();
    void shutdown();
    uint32_t pick(const SceneGraph& graph, const Scene& flatScene, const Camera& camera,
                  int width, int height, float u, float v);
};

} // namespace photon
