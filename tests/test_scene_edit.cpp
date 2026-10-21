// test_scene_edit.cpp — Sahne paneli işlemleri: taşıma (reparent), gruplama, grubu
// çözme dünya konumunu korumalı, döngü oluşturmamalı; içe aktarılmış modelden
// kopyalanan/çıkarılan parçalar proje kaydından sağ çıkmalı.
#include <gtest/gtest.h>
#include "scene/scene_graph.h"
#include "scene/project_io.h"
#include "scene/model_import.h"
#include "core/platform/path.h"
#include <filesystem>
#include <fstream>

using namespace photon;

namespace {

SceneNode* addMesh(SceneNode& parent, const char* name, const Transform& xf) {
    auto n = std::make_unique<SceneNode>(name, SceneNodeType::Mesh);
    n->mesh = std::make_shared<TriangleMesh>(std::vector<Vec3f>{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, std::vector<Vec3f>{},
                                             std::vector<Vec2f>{}, std::vector<uint32_t>{0, 1, 2}, nullptr);
    n->localTransform = xf;
    return parent.addChild(std::move(n));
}

SceneNode* addGroup(SceneNode& parent, const char* name, const Transform& xf) {
    auto n = std::make_unique<SceneNode>(name, SceneNodeType::Group);
    n->localTransform = xf;
    return parent.addChild(std::move(n));
}

void expectSameMatrix(const Mat4f& a, const Mat4f& b) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) EXPECT_NEAR(a(r, c), b(r, c), 1e-4f) << "(" << r << "," << c << ")";
}

} // namespace

TEST(SceneEdit, ReparentKeepsWorldTransform) {
    SceneGraph g;
    SceneNode* a = addGroup(*g.root(), "A", Transform::translate(Vec3f(1, 2, 3)) * Transform::rotateY(0.7f));
    SceneNode* b = addGroup(*g.root(), "B", Transform::scale(Vec3f(2.0f)) * Transform::rotateX(-0.3f));
    SceneNode* m = addMesh(*a, "m", Transform::translate(Vec3f(0.5f, 0, 0)));
    const Mat4f before = m->worldTransform().matrix();
    ASSERT_TRUE(g.reparent(m, b));
    EXPECT_EQ(m->parent, b);
    EXPECT_TRUE(a->children.empty());
    expectSameMatrix(m->worldTransform().matrix(), before);
}

TEST(SceneEdit, ReparentRejectsCycles) {
    SceneGraph g;
    SceneNode* a = addGroup(*g.root(), "A", Transform{});
    SceneNode* b = addGroup(*a, "B", Transform{});
    EXPECT_FALSE(g.reparent(a, b)); // kendi çocuğunun altına
    EXPECT_FALSE(g.reparent(a, a));
    EXPECT_FALSE(g.reparent(g.root(), a));
    EXPECT_EQ(b->parent, a);
}

TEST(SceneEdit, ReorderWithinSameParent) {
    SceneGraph g;
    SceneNode* a = addMesh(*g.root(), "a", Transform{});
    addMesh(*g.root(), "b", Transform{});
    addMesh(*g.root(), "c", Transform{});
    ASSERT_TRUE(g.reparent(a, g.root(), 3)); // sona (eski konumu çıkarılınca indeks 2)
    EXPECT_EQ(g.root()->children[0]->name, "b");
    EXPECT_EQ(g.root()->children[2]->name, "a");
}

// Sürükle-bırak: [a b c d e] içinde a ve c'yi d'nin arkasına (sıra 4) taşı → [b d a c e].
// Ayrıca başka ebeveynden gelen düğümler sırayla, ardışık yerleşmeli.
TEST(SceneEdit, MoveSeveralKeepsTheirOrder) {
    SceneGraph g;
    SceneNode* a = addMesh(*g.root(), "a", Transform{});
    addMesh(*g.root(), "b", Transform{});
    SceneNode* c = addMesh(*g.root(), "c", Transform{});
    addMesh(*g.root(), "d", Transform{});
    addMesh(*g.root(), "e", Transform{});
    EXPECT_EQ(g.move({c, a}, g.root(), 4), 2u);
    std::string order;
    for (const auto& ch : g.root()->children) order += ch->name;
    EXPECT_EQ(order, "bdace");

    SceneNode* grp = addGroup(*g.root(), "G", Transform{});
    addMesh(*grp, "x", Transform{});
    EXPECT_EQ(g.move({a, c, grp}, grp, 0), 2u); // grubun kendisi atlanır
    ASSERT_EQ(grp->children.size(), 3u);
    EXPECT_EQ(grp->children[0]->name, "a");
    EXPECT_EQ(grp->children[1]->name, "c");
    EXPECT_EQ(grp->children[2]->name, "x");
}

TEST(SceneEdit, GroupAndUngroupKeepWorldAndOrder) {
    SceneGraph g;
    SceneNode* p = addGroup(*g.root(), "P", Transform::translate(Vec3f(0, 1, 0)));
    addMesh(*p, "x", Transform{});
    SceneNode* m1 = addMesh(*p, "m1", Transform::rotateZ(0.4f));
    SceneNode* m2 = addMesh(*p, "m2", Transform::translate(Vec3f(3, 0, 0)));
    const Mat4f w1 = m1->worldTransform().matrix(), w2 = m2->worldTransform().matrix();

    SceneNode* grp = g.group({m2, m1}, "Grup");
    ASSERT_TRUE(grp);
    EXPECT_EQ(grp->parent, p);               // ortak ebeveyn
    EXPECT_EQ(p->children[1].get(), grp);    // ilk düğümün yerinde
    ASSERT_EQ(grp->children.size(), 2u);
    EXPECT_EQ(grp->children[0]->name, "m1"); // ağaç sırası korunur
    expectSameMatrix(m1->worldTransform().matrix(), w1);
    expectSameMatrix(m2->worldTransform().matrix(), w2);

    grp->localTransform = Transform::translate(Vec3f(0, 0, 5)); // grup taşınmış olsun
    const Mat4f w1g = m1->worldTransform().matrix();
    const auto moved = g.ungroup(grp);
    ASSERT_EQ(moved.size(), 2u);
    ASSERT_EQ(p->children.size(), 3u);
    EXPECT_EQ(p->children[1]->name, "m1");
    EXPECT_EQ(p->children[2]->name, "m2");
    expectSameMatrix(m1->worldTransform().matrix(), w1g);
}

TEST(SceneEdit, TopmostDropsDescendantsOfSelectedGroups) {
    SceneGraph g;
    SceneNode* a = addGroup(*g.root(), "A", Transform{});
    SceneNode* a1 = addMesh(*a, "a1", Transform{});
    SceneNode* b = addMesh(*g.root(), "b", Transform{});
    const auto top = g.topmost({b, a1, a});
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0], a); // ağaç sırası
    EXPECT_EQ(top[1], b);
}

// İçe aktarılmış modelin bir parçası aynı grupta çoğaltılır, bir diğeri gruptan
// çıkarılır: proje kaydedilip açılınca tüm parçalar yerinde olmalı.
TEST(SceneEdit, CopiedAndMovedSourcePartsSurviveProjectRoundTrip) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "photon_scene_edit";
    fs::remove_all(dir);
    fs::create_directories(dir);
    // İki ayrı nesneli küçük bir OBJ.
    const fs::path obj = dir / "iki.obj";
    {
        std::ofstream f(obj);
        f << "o bir\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"
             "o iki\nv 0 0 1\nv 1 0 1\nv 0 1 1\nf 4 5 6\n";
    }
    std::string err;
    auto model = importModelFile(pathToUtf8(obj), &err);
    ASSERT_TRUE(model) << err;
    ASSERT_EQ(model->children.size(), 2u);
    SceneGraph g;
    SceneNode* src = g.root()->addChild(std::move(model));

    auto copy = SceneGraph::cloneTree(*src->children[0], false);
    SceneGraph::detachFromSource(*copy);
    copy->name = "bir kopya";
    src->addChild(std::move(copy));
    ASSERT_TRUE(g.reparent(src->children[1].get(), g.root())); // "iki" dışarı

    const std::string file = pathToUtf8(dir / "p.photon");
    ProjectData data;
    ASSERT_TRUE(saveProject(file, g, data, &err)) << err;
    SceneGraph back;
    ProjectData d2;
    ASSERT_TRUE(loadProject(file, back, d2, &err)) << err;
    ASSERT_EQ(back.root()->children.size(), 2u);
    const SceneNode& grp = *back.root()->children[0];
    ASSERT_EQ(grp.children.size(), 2u);
    EXPECT_TRUE(grp.children[0]->mesh);
    EXPECT_TRUE(grp.children[1]->mesh);
    EXPECT_EQ(grp.children[1]->name, "bir kopya");
    EXPECT_TRUE(back.root()->children[1]->mesh);
    fs::remove_all(dir);
}
