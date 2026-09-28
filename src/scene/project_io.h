#pragma once

#include "scene/scene_graph.h"
#include <functional>
#include <string>
#include <vector>

namespace photon {

class UndoStack {
public:
    using Command = std::function<void()>;

    void push(Command undo, Command redo);
    void undo();
    void redo();
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_undo.size(); }
    void clear();

private:
    std::vector<Command> m_undo;
    std::vector<Command> m_redo;
    size_t m_cursor = 0;
};

/// .photon JSON: camera blob + graph imports / material / transform overrides.
bool saveProject(const std::string& path, const SceneGraph& graph, const std::string& cameraJson,
                 const std::string& environmentPath = {});
bool loadProject(const std::string& path, SceneGraph& graph, std::string& jsonOut);

/// Parse helpers for Application (minimal JSON, no deps).
struct ProjectImport {
    std::string path;
};
struct ProjectMaterial {
    std::string nodePath;
    float baseColor[3] = {0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float clearCoat = 0.0f;
    float clearCoatRoughness = 0.03f;
    std::string albedoMap;
    std::string normalMap;
    std::string roughnessMap;
    std::string metalnessMap;
};
struct ProjectTransform {
    std::string nodePath;
    float translate[3] = {0, 0, 0};
    float rotateDeg[3] = {0, 0, 0}; ///< XYZ Euler degrees
    float scale[3] = {1, 1, 1};
    bool hasRotateScale = false;
};
struct ProjectFile {
    int version = 1;
    std::string cameraJson = "{}";
    std::string environmentPath;
    bool includeCornell = true;
    std::vector<ProjectImport> imports;
    std::vector<ProjectMaterial> materials;
    std::vector<ProjectTransform> transforms;
};

std::string buildProjectJson(const SceneGraph& graph, const std::string& cameraJson,
                             const std::string& environmentPath);
bool parseProjectJson(const std::string& json, ProjectFile& out);
SceneNode* findNodeByPath(SceneNode* root, const std::string& path);
std::string nodePath(const SceneNode* node);

} // namespace photon
