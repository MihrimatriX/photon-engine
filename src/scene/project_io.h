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

bool saveProject(const std::string& path, const SceneGraph& graph, const std::string& cameraJson);
bool loadProject(const std::string& path, SceneGraph& graph, std::string& cameraJson);

} // namespace photon
