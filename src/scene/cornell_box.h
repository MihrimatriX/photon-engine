// cornell_box.h — Klasik Cornell kutusu test sahnesini SceneGraph'a kuran yardımcılar.
// 555 birimlik kutu: kırmızı/yeşil yan duvarlar renk taşmasını (color bleeding), cam küre
// kırılma ve kostikleri, altın küre metal BRDF'i, ayna kutu yansımaları sınar.
#pragma once

#include "scene/scene_graph.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "materials/dielectric.h"
#include "materials/disney.h"
#include <memory>

namespace photon {

// Dörtgen = iki üçgen (0,1,2) ve (0,2,3); köşeler çevre sırasıyla verilmeli, n tüm köşelere atanır.
inline void addQuad(SceneGraph& graph, SceneNode* parent,
                    const Vec3f& v0, const Vec3f& v1, const Vec3f& v2, const Vec3f& v3,
                    const Vec3f& n, std::shared_ptr<Material> mat, const char* name) {
    auto node = std::make_unique<SceneNode>(name, SceneNodeType::Mesh);
    node->material = mat;
    std::vector<Vec3f> pos = {v0, v1, v2, v3};
    std::vector<Vec3f> norms = {n, n, n, n};
    std::vector<uint32_t> idx = {0, 1, 2, 0, 2, 3};
    node->mesh = std::make_shared<TriangleMesh>(pos, norms, std::vector<Vec2f>{}, idx, mat.get());
    node->pickId = graph.allocatePickId();
    parent->addChild(std::move(node));
}

// minP–maxP eksen hizalı kutusu: altı dörtgen, normaller dışa bakar.
inline void addBox(SceneGraph& graph, SceneNode* parent,
                   const Vec3f& minP, const Vec3f& maxP, std::shared_ptr<Material> mat, const char* name) {
    Vec3f v0(minP.x, minP.y, minP.z), v1(maxP.x, minP.y, minP.z);
    Vec3f v2(maxP.x, maxP.y, minP.z), v3(minP.x, maxP.y, minP.z);
    Vec3f v4(minP.x, minP.y, maxP.z), v5(maxP.x, minP.y, maxP.z);
    Vec3f v6(maxP.x, maxP.y, maxP.z), v7(minP.x, maxP.y, maxP.z);
    addQuad(graph, parent, v0, v3, v2, v1, Vec3f(0, 0, -1), mat, "box_front");
    addQuad(graph, parent, v1, v2, v6, v5, Vec3f(1, 0, 0), mat, "box_right");
    addQuad(graph, parent, v5, v6, v7, v4, Vec3f(0, 0, 1), mat, "box_back");
    addQuad(graph, parent, v4, v7, v3, v0, Vec3f(-1, 0, 0), mat, "box_left");
    addQuad(graph, parent, v3, v7, v6, v2, Vec3f(0, 1, 0), mat, "box_top");
    addQuad(graph, parent, v4, v0, v1, v5, Vec3f(0, -1, 0), mat, name);
}

inline void buildCornellBox(SceneGraph& graph) {
    auto cornell = std::make_unique<SceneNode>("Cornell Box", SceneNodeType::Group);
    SceneNode* root = cornell.get();

    auto red = std::make_shared<Lambertian>(Color3f(0.65f, 0.05f, 0.05f));
    auto green = std::make_shared<Lambertian>(Color3f(0.12f, 0.45f, 0.15f));
    auto white = std::make_shared<Lambertian>(Color3f(0.73f, 0.73f, 0.73f));
    auto mirror = std::make_shared<Mirror>(Color3f(0.95f));
    auto glass = std::make_shared<Dielectric>(1.5f, Color3f(1.0f));
    auto gold = std::make_shared<DisneyMaterial>(Color3f(1.0f, 0.782f, 0.344f), 0.9f, 0.2f, 0.5f);

    addQuad(graph, root, Vec3f(0, 0, 0), Vec3f(0, 0, 555), Vec3f(555, 0, 555), Vec3f(555, 0, 0), Vec3f(0, 1, 0), white, "floor");
    addQuad(graph, root, Vec3f(0, 555, 0), Vec3f(555, 555, 0), Vec3f(555, 555, 555), Vec3f(0, 555, 555), Vec3f(0, -1, 0), white, "ceiling");
    addQuad(graph, root, Vec3f(0, 0, 555), Vec3f(555, 0, 555), Vec3f(555, 555, 555), Vec3f(0, 555, 555), Vec3f(0, 0, -1), white, "back");
    addQuad(graph, root, Vec3f(0, 0, 0), Vec3f(0, 555, 0), Vec3f(0, 555, 555), Vec3f(0, 0, 555), Vec3f(1, 0, 0), red, "left");
    addQuad(graph, root, Vec3f(555, 0, 0), Vec3f(555, 0, 555), Vec3f(555, 555, 555), Vec3f(555, 555, 0), Vec3f(-1, 0, 0), green, "right");

    {
        auto sphere = std::make_unique<SceneNode>("Glass Sphere", SceneNodeType::Sphere);
        sphere->material = glass;
        sphere->sphereRadius = 100.0f;
        sphere->localTransform = Transform::translate(Vec3f(150, 100, 150));
        sphere->pickId = graph.allocatePickId();
        root->addChild(std::move(sphere));
    }
    {
        auto sphere = std::make_unique<SceneNode>("Gold Sphere", SceneNodeType::Sphere);
        sphere->material = gold;
        sphere->sphereRadius = 90.0f;
        sphere->localTransform = Transform::translate(Vec3f(278, 90, 278));
        sphere->pickId = graph.allocatePickId();
        root->addChild(std::move(sphere));
    }
    addBox(graph, root, Vec3f(360, 0, 320), Vec3f(490, 200, 450), mirror, "mirror_box");

    graph.root()->addChild(std::move(cornell));
}

} // namespace photon
