#pragma once

#include "scene/scene_graph.h"
#include "engine/scene.h"
#include "camera/camera.h"
#include "core/image/image.h"
#include "core/math/mat.h"
#include "core/math/vec.h"
#include <cstdint>

namespace photon {

enum class PreviewQuality { Fast, Quality };

class GLPreview {
public:
    void init();
    void shutdown();

    /// Rasterize IBL+GGX preview into an FBO. Call from the GL thread only.
    void render(const SceneGraph& graph,
                const Mat4f& view, const Mat4f& proj, const Vec3f& camPos,
                int width, int height, PreviewQuality quality,
                const Image* envMap = nullptr, float exposure = 0.0f);

    unsigned int colorTexture() const { return m_colorTex; }
    bool ready() const { return m_ready; }

private:
    bool m_ready = false;
    bool m_glLoaded = false;
    unsigned int m_fbo = 0;
    unsigned int m_colorTex = 0;
    unsigned int m_depthRbo = 0;
    unsigned int m_meshProg = 0;
    unsigned int m_skyProg = 0;
    unsigned int m_cubeVao = 0;
    unsigned int m_cubeVbo = 0;
    unsigned int m_sphereVao = 0;
    unsigned int m_sphereVbo = 0;
    unsigned int m_sphereIbo = 0;
    int m_sphereIndexCount = 0;
    unsigned int m_envCube = 0;
    int m_fbW = 0;
    int m_fbH = 0;
    const Image* m_lastEnv = nullptr;
    int m_lastEnvW = 0;
    int m_lastEnvH = 0;
    int m_cubeSize = 0;

    bool loadGL();
    unsigned int compileProgram(const char* vs, const char* fs);
    void ensureFbo(int w, int h);
    void ensureEnvCube(const Image* envMap, int faceSize);
    void drawNode(const SceneNode& node, const Transform& parent,
                  const Mat4f& view, const Mat4f& proj, const Vec3f& camPos, float exposure);
    void drawMesh(const TriangleMesh& mesh, const Mat4f& model,
                  const Material* mat, const Mat4f& view, const Mat4f& proj,
                  const Vec3f& camPos, float exposure);
    void drawSphere(float radius, const Mat4f& model, const Material* mat,
                    const Mat4f& view, const Mat4f& proj, const Vec3f& camPos, float exposure);
};

class PickingPass {
public:
    void init();
    void shutdown();
    uint32_t pick(const SceneGraph& graph, const Scene& flatScene, const Camera& camera,
                  int width, int height, float u, float v);
};

} // namespace photon
