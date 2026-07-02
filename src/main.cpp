#include "engine/scene.h"
#include "engine/renderer.h"
#include "camera/perspective_camera.h"
#include "materials/lambertian.h"
#include "materials/mirror.h"
#include "materials/dielectric.h"
#include "materials/disney.h"
#include "lights/area_light.h"
#include "geometry/sphere.h"
#include "geometry/triangle.h"
#include "core/image/image_io.h"
#include <iostream>
#include <memory>
#include <chrono>

using namespace photon;

// Helper to add a quad (2 triangles) to the scene
void addQuad(Scene& scene, 
             const Vec3f& v0, const Vec3f& v1, const Vec3f& v2, const Vec3f& v3,
             const Vec3f& n, const Material* mat) {
    Vec2f uv0(0.0f, 0.0f);
    Vec2f uv1(1.0f, 0.0f);
    Vec2f uv2(1.0f, 1.0f);
    Vec2f uv3(0.0f, 1.0f);

    scene.addShape(std::make_shared<Triangle>(v0, v1, v2, n, n, n, uv0, uv1, uv2, mat));
    scene.addShape(std::make_shared<Triangle>(v0, v2, v3, n, n, n, uv0, uv2, uv3, mat));
}

// Helper to add a box to the scene
void addBox(Scene& scene, const Vec3f& minP, const Vec3f& maxP, const Material* mat) {
    // 8 vertices
    Vec3f v0(minP.x, minP.y, minP.z);
    Vec3f v1(maxP.x, minP.y, minP.z);
    Vec3f v2(maxP.x, maxP.y, minP.z);
    Vec3f v3(minP.x, maxP.y, minP.z);
    Vec3f v4(minP.x, minP.y, maxP.z);
    Vec3f v5(maxP.x, minP.y, maxP.z);
    Vec3f v6(maxP.x, maxP.y, maxP.z);
    Vec3f v7(minP.x, maxP.y, maxP.z);

    // 6 faces
    addQuad(scene, v0, v3, v2, v1, Vec3f(0, 0, -1), mat); // Front
    addQuad(scene, v1, v2, v6, v5, Vec3f(1, 0, 0), mat);  // Right
    addQuad(scene, v5, v6, v7, v4, Vec3f(0, 0, 1), mat);  // Back
    addQuad(scene, v4, v7, v3, v0, Vec3f(-1, 0, 0), mat); // Left
    addQuad(scene, v3, v7, v6, v2, Vec3f(0, 1, 0), mat);  // Top
    addQuad(scene, v4, v0, v1, v5, Vec3f(0, -1, 0), mat); // Bottom
}

int main() {
    auto totalStart = std::chrono::high_resolution_clock::now();

    Scene scene;

    // ─── 1. Materials ────────────────────────────────────────────────────────
    auto red = std::make_shared<Lambertian>(Color3f(0.65f, 0.05f, 0.05f));
    auto green = std::make_shared<Lambertian>(Color3f(0.12f, 0.45f, 0.15f));
    auto white = std::make_shared<Lambertian>(Color3f(0.73f, 0.73f, 0.73f));
    
    // Specular / Glass materials
    auto mirror = std::make_shared<Mirror>(Color3f(0.95f));
    auto glass = std::make_shared<Dielectric>(1.5f, Color3f(1.0f));

    // Disney Material for a metallic-rough ball
    auto goldDisney = std::make_shared<DisneyMaterial>(
        Color3f(1.0f, 0.782f, 0.344f), // Gold F0 color
        0.9f,                          // metallic
        0.2f,                          // roughness
        0.5f                           // specular
    );

    // ─── 2. Cornell Box Geometry ─────────────────────────────────────────────
    // Coordinates are standard Cornell Box dimensions [0, 555]
    
    // Floor (white)
    addQuad(scene, Vec3f(0, 0, 0), Vec3f(0, 0, 555), Vec3f(555, 0, 555), Vec3f(555, 0, 0), Vec3f(0, 1, 0), white.get());
    
    // Ceiling (white)
    addQuad(scene, Vec3f(0, 555, 0), Vec3f(555, 555, 0), Vec3f(555, 555, 555), Vec3f(0, 555, 555), Vec3f(0, -1, 0), white.get());
    
    // Back wall (white)
    addQuad(scene, Vec3f(0, 0, 555), Vec3f(555, 0, 555), Vec3f(555, 555, 555), Vec3f(0, 555, 555), Vec3f(0, 0, -1), white.get());
    
    // Left wall (red)
    addQuad(scene, Vec3f(0, 0, 0), Vec3f(0, 555, 0), Vec3f(0, 555, 555), Vec3f(0, 0, 555), Vec3f(1, 0, 0), red.get());
    
    // Right wall (green)
    addQuad(scene, Vec3f(555, 0, 0), Vec3f(555, 0, 555), Vec3f(555, 555, 555), Vec3f(555, 555, 0), Vec3f(-1, 0, 0), green.get());

    // ─── 3. Objects inside Box ───────────────────────────────────────────────
    // Glass sphere on the left floor
    scene.addShape(std::make_shared<Sphere>(Vec3f(150, 100, 150), 100.0f, glass.get()));

    // Gold Disney sphere in the middle
    scene.addShape(std::make_shared<Sphere>(Vec3f(278, 90, 278), 90.0f, goldDisney.get()));

    // Mirror block on the right floor
    addBox(scene, Vec3f(360, 0, 320), Vec3f(490, 200, 450), mirror.get());

    // ─── 4. Light Sources ────────────────────────────────────────────────────
    // Ceiling area light
    Vec3f lightPos(343, 548.0f, 227);
    Vec3f lightU(-130, 0, 0);
    Vec3f lightV(0, 0, 105);
    Color3f lightRadiance(15.0f);
    
    auto areaLight = std::make_shared<AreaLight>(lightPos, lightU, lightV, lightRadiance);
    scene.addLight(areaLight);

    // ─── 5. Acceleration Structure ───────────────────────────────────────────
    std::cout << "Building BVH..." << std::endl;
    scene.buildAccelerator();

    // ─── 6. Camera Setup ─────────────────────────────────────────────────────
    Vec3f camPos(278, 273, -800);
    Vec3f camTarget(278, 273, 0);
    Vec3f camUp(0, 1, 0);
    float fov = 40.0f;
    float aspect = 1.0f; // Square render

    PerspectiveCamera camera(camPos, camTarget, camUp, fov, aspect);

    // ─── 7. Render ───────────────────────────────────────────────────────────
    RenderSettings settings;
    settings.width = 512;
    settings.height = 512;
    settings.samplesPerPixel = 64; // Set 64 SPP for a quick, clean render
    settings.maxBounces = 8;
    settings.tileSize = 32;
    settings.tmo = ToneMapOperator::ACES;
    settings.exposure = 0.0f;

    Renderer renderer;
    Image outputImg = renderer.render(scene, camera, settings);

    // ─── 8. Save Output ──────────────────────────────────────────────────────
    std::string outputPath = "output.png";
    std::cout << "Saving image to " << outputPath << "..." << std::endl;
    if (saveImagePNG(outputImg, outputPath, settings.tmo, settings.exposure)) {
        std::cout << "Success!" << std::endl;
    } else {
        std::cerr << "Failed to save image." << std::endl;
    }

    auto totalEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> totalElapsed = totalEnd - totalStart;
    std::cout << "Total execution time: " << totalElapsed.count() << " seconds." << std::endl;

    return 0;
}
