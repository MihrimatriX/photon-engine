#include "scene/project_io.h"
#include <fstream>

namespace photon {

void UndoStack::push(Command undo, Command redo) {
    while (m_undo.size() > m_cursor) m_undo.pop_back();
    m_undo.push_back(std::move(undo));
    m_redo.push_back(std::move(redo));
    ++m_cursor;
}

void UndoStack::undo() {
    if (!canUndo()) return;
    --m_cursor;
    m_undo[m_cursor]();
}

void UndoStack::redo() {
    if (!canRedo()) return;
    m_redo[m_cursor]();
    ++m_cursor;
}

void UndoStack::clear() {
    m_undo.clear();
    m_redo.clear();
    m_cursor = 0;
}

bool saveProject(const std::string& path, const SceneGraph& graph, const std::string& cameraJson) {
    (void)graph;
    std::ofstream out(path);
    if (!out) return false;
    // ponytail: minimal .photon stub — camera + version only; full graph v2
    out << "{\n  \"version\": 1,\n  \"camera\": " << cameraJson << "\n}\n";
    return true;
}

bool loadProject(const std::string& path, SceneGraph& graph, std::string& cameraJson) {
    (void)graph;
    std::ifstream in(path);
    if (!in) return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    cameraJson = content;
    return true;
}

} // namespace photon
