#include "scene/project_io.h"
#include "materials/disney.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace photon {
namespace {

float jsonFloatField(const std::string& json, const std::string& key, float def) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    return std::strtof(json.c_str() + pos + 1, nullptr);
}

std::string jsonStringField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = json.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = json.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return json.substr(pos + 1, end - pos - 1);
}

bool jsonBoolField(const std::string& json, const std::string& key, bool def) {
    std::string needle = "\"" + key + "\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return def;
    auto t = json.find("true", pos);
    auto f = json.find("false", pos);
    if (t != std::string::npos && (f == std::string::npos || t < f)) return true;
    if (f != std::string::npos) return false;
    return def;
}

void escapeJson(std::ostringstream& out, const std::string& s) {
    for (char c : s) {
        if (c == '\\' || c == '"') out << '\\';
        out << c;
    }
}

void collectNodes(const SceneNode* n, std::vector<const SceneNode*>& out) {
    if (!n) return;
    out.push_back(n);
    for (const auto& c : n->children) collectNodes(c.get(), out);
}

} // namespace

void UndoStack::push(Command undo, Command redo) {
    while (m_undo.size() > m_cursor) {
        m_undo.pop_back();
        m_redo.pop_back();
    }
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

std::string nodePath(const SceneNode* node) {
    if (!node || !node->parent) return {};
    std::vector<std::string> parts;
    for (const SceneNode* n = node; n && n->parent; n = n->parent) parts.push_back(n->name);
    std::ostringstream oss;
    for (int i = static_cast<int>(parts.size()) - 1; i >= 0; --i) {
        if (i < static_cast<int>(parts.size()) - 1) oss << '/';
        oss << parts[static_cast<size_t>(i)];
    }
    return oss.str();
}

SceneNode* findNodeByPath(SceneNode* root, const std::string& path) {
    if (!root || path.empty()) return nullptr;
    SceneNode* cur = root;
    size_t start = 0;
    while (start < path.size()) {
        size_t slash = path.find('/', start);
        std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        SceneNode* next = nullptr;
        for (auto& c : cur->children) {
            if (c->name == part) { next = c.get(); break; }
        }
        if (!next) return nullptr;
        cur = next;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return cur == root ? nullptr : cur;
}

std::string buildProjectJson(const SceneGraph& graph, const std::string& cameraJson,
                             const std::string& environmentPath) {
    bool includeCornell = false;
    std::ostringstream out;
    out << "{\n  \"version\": 1,\n  \"camera\": " << (cameraJson.empty() ? "{}" : cameraJson) << ",\n";
    out << "  \"environment\": \"";
    escapeJson(out, environmentPath);
    out << "\",\n";

    out << "  \"imports\": [";
    bool firstImp = true;
    for (const auto& c : graph.root()->children) {
        if (c->name == "Cornell Box") includeCornell = true;
        if (c->sourcePath.empty()) continue;
        if (!firstImp) out << ", ";
        firstImp = false;
        out << "\"";
        escapeJson(out, c->sourcePath);
        out << "\"";
    }
    out << "],\n";
    out << "  \"includeCornell\": " << (includeCornell ? "true" : "false") << ",\n";

    out << "  \"materials\": [\n";
    std::vector<const SceneNode*> nodes;
    collectNodes(graph.root(), nodes);
    bool firstMat = true;
    for (const SceneNode* n : nodes) {
        if (!n->parent) continue;
        auto disney = std::dynamic_pointer_cast<DisneyMaterial>(n->material);
        if (!disney) continue;
        if (!firstMat) out << ",\n";
        firstMat = false;
        Color3f bc = disney->baseColor();
        out << "    {\"path\": \"";
        escapeJson(out, nodePath(n));
        out << "\", \"baseColor\": [" << bc.r << "," << bc.g << "," << bc.b
            << "], \"metallic\": " << disney->metallic()
            << ", \"roughness\": " << disney->roughness()
            << ", \"specular\": " << disney->specular()
            << ", \"clearCoat\": " << disney->clearCoat()
            << ", \"clearCoatRoughness\": " << disney->clearCoatRoughness();
        auto writeMap = [&](const char* key, const std::string& path) {
            if (path.empty()) return;
            out << ", \"" << key << "\": \"";
            escapeJson(out, path);
            out << "\"";
        };
        writeMap("albedoMap", disney->albedoMap());
        writeMap("normalMap", disney->normalMap());
        writeMap("roughnessMap", disney->roughnessMap());
        writeMap("metalnessMap", disney->metalnessMap());
        out << "}";
    }
    out << "\n  ],\n";

    out << "  \"transforms\": [\n";
    bool firstXf = true;
    for (const SceneNode* n : nodes) {
        if (!n->parent) continue;
        const Mat4f& m = n->localTransform.matrix();
        float tx = m(0, 3), ty = m(1, 3), tz = m(2, 3);
        float sx = std::sqrt(m(0, 0) * m(0, 0) + m(1, 0) * m(1, 0) + m(2, 0) * m(2, 0));
        float sy = std::sqrt(m(0, 1) * m(0, 1) + m(1, 1) * m(1, 1) + m(2, 1) * m(2, 1));
        float sz = std::sqrt(m(0, 2) * m(0, 2) + m(1, 2) * m(1, 2) + m(2, 2) * m(2, 2));
        sx = std::max(sx, 1e-8f);
        sy = std::max(sy, 1e-8f);
        sz = std::max(sz, 1e-8f);

        // XYZ Euler from rotation part (columns = axes)
        float r00 = m(0, 0) / sx, r10 = m(1, 0) / sx, r20 = m(2, 0) / sx;
        float r21 = m(2, 1) / sy, r22 = m(2, 2) / sz;
        float r01 = m(0, 1) / sy, r11 = m(1, 1) / sy;
        float ry = std::asin(std::clamp(-r20, -1.0f, 1.0f));
        float rx, rz;
        if (std::cos(ry) > 1e-6f) {
            rx = std::atan2(r21, r22);
            rz = std::atan2(r10, r00);
        } else {
            rx = std::atan2(-r01, r11);
            rz = 0.0f;
        }
        constexpr float kRad2Deg = 180.0f / 3.14159265f;
        float rdx = rx * kRad2Deg, rdy = ry * kRad2Deg, rdz = rz * kRad2Deg;

        bool identity = (tx == 0.0f && ty == 0.0f && tz == 0.0f)
            && (std::fabs(sx - 1.0f) < 1e-5f && std::fabs(sy - 1.0f) < 1e-5f && std::fabs(sz - 1.0f) < 1e-5f)
            && (std::fabs(rdx) < 1e-3f && std::fabs(rdy) < 1e-3f && std::fabs(rdz) < 1e-3f);
        if (identity) continue;

        if (!firstXf) out << ",\n";
        firstXf = false;
        out << "    {\"path\": \"";
        escapeJson(out, nodePath(n));
        out << "\", \"translate\": [" << tx << "," << ty << "," << tz
            << "], \"rotate\": [" << rdx << "," << rdy << "," << rdz
            << "], \"scale\": [" << sx << "," << sy << "," << sz << "]}";
    }
    out << "\n  ]\n}\n";
    return out.str();
}

bool parseProjectJson(const std::string& json, ProjectFile& out) {
    out = ProjectFile{};
    out.version = static_cast<int>(jsonFloatField(json, "version", 1.0f));
    out.includeCornell = jsonBoolField(json, "includeCornell", true);
    out.environmentPath = jsonStringField(json, "environment");

    auto camPos = json.find("\"camera\"");
    if (camPos != std::string::npos) {
        auto brace = json.find('{', camPos);
        if (brace != std::string::npos) {
            int depth = 0;
            size_t end = brace;
            for (; end < json.size(); ++end) {
                if (json[end] == '{') ++depth;
                else if (json[end] == '}') {
                    --depth;
                    if (depth == 0) { ++end; break; }
                }
            }
            out.cameraJson = json.substr(brace, end - brace);
        }
    }

    auto impPos = json.find("\"imports\"");
    if (impPos != std::string::npos) {
        auto arr = json.find('[', impPos);
        auto arrEnd = json.find(']', arr);
        if (arr != std::string::npos && arrEnd != std::string::npos) {
            size_t i = arr + 1;
            while (i < arrEnd) {
                auto q0 = json.find('"', i);
                if (q0 == std::string::npos || q0 >= arrEnd) break;
                auto q1 = json.find('"', q0 + 1);
                if (q1 == std::string::npos || q1 >= arrEnd) break;
                out.imports.push_back({json.substr(q0 + 1, q1 - q0 - 1)});
                i = q1 + 1;
            }
        }
    }

    // ponytail: scan material / transform objects by repeated {"path":
    size_t search = 0;
    while ((search = json.find("{\"path\":", search)) != std::string::npos) {
        auto objEnd = json.find('}', search);
        if (objEnd == std::string::npos) break;
        std::string obj = json.substr(search, objEnd - search + 1);
        std::string path = jsonStringField(obj, "path");
        if (obj.find("\"baseColor\"") != std::string::npos) {
            ProjectMaterial m;
            m.nodePath = path;
            auto bc = obj.find('[');
            if (bc != std::string::npos)
                std::sscanf(obj.c_str() + bc, "[%f,%f,%f]", &m.baseColor[0], &m.baseColor[1], &m.baseColor[2]);
            m.metallic = jsonFloatField(obj, "metallic", 0.0f);
            m.roughness = jsonFloatField(obj, "roughness", 0.5f);
            m.specular = jsonFloatField(obj, "specular", 0.5f);
            m.clearCoat = jsonFloatField(obj, "clearCoat", 0.0f);
            m.clearCoatRoughness = jsonFloatField(obj, "clearCoatRoughness", 0.03f);
            m.albedoMap = jsonStringField(obj, "albedoMap");
            m.normalMap = jsonStringField(obj, "normalMap");
            m.roughnessMap = jsonStringField(obj, "roughnessMap");
            m.metalnessMap = jsonStringField(obj, "metalnessMap");
            out.materials.push_back(m);
        } else if (obj.find("\"translate\"") != std::string::npos || obj.find("\"rotate\"") != std::string::npos
                   || obj.find("\"scale\"") != std::string::npos) {
            ProjectTransform t;
            t.nodePath = path;
            auto tr = obj.find("\"translate\"");
            if (tr != std::string::npos) {
                auto br = obj.find('[', tr);
                if (br != std::string::npos)
                    std::sscanf(obj.c_str() + br, "[%f,%f,%f]", &t.translate[0], &t.translate[1], &t.translate[2]);
            }
            auto rr = obj.find("\"rotate\"");
            if (rr != std::string::npos) {
                auto br = obj.find('[', rr);
                if (br != std::string::npos) {
                    std::sscanf(obj.c_str() + br, "[%f,%f,%f]", &t.rotateDeg[0], &t.rotateDeg[1], &t.rotateDeg[2]);
                    t.hasRotateScale = true;
                }
            }
            auto sc = obj.find("\"scale\"");
            if (sc != std::string::npos) {
                auto br = obj.find('[', sc);
                if (br != std::string::npos) {
                    std::sscanf(obj.c_str() + br, "[%f,%f,%f]", &t.scale[0], &t.scale[1], &t.scale[2]);
                    t.hasRotateScale = true;
                }
            }
            out.transforms.push_back(t);
        }
        search = objEnd + 1;
    }
    return true;
}

bool saveProject(const std::string& path, const SceneGraph& graph, const std::string& cameraJson,
                 const std::string& environmentPath) {
    std::ofstream out(path);
    if (!out) return false;
    out << buildProjectJson(graph, cameraJson, environmentPath);
    return true;
}

bool loadProject(const std::string& path, SceneGraph& graph, std::string& cameraJson) {
    (void)graph;
    std::ifstream in(path);
    if (!in) return false;
    cameraJson.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return !cameraJson.empty();
}

} // namespace photon
