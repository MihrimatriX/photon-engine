# Tarama bulguları (2026-10-02)

22 ajanlı salt-okunur denetimin doğrulanmış sonucu. Her bulgu ikinci bir ajan tarafından kodla yeniden kontrol edildi.
Görevler (`01-gorevler.md`) bu dosyadaki kimliklere atıf yapar, ör. `[engine-1]`. Satır numaraları 2026-10-02 çalışma ağacına aittir; kod değiştikçe kayar, uygulamadan önce yeniden bakılmalı.

Hüküm: **confirmed** = doğrulayıcı koddan kendisi türetti, **plausible** = büyük olasılıkla doğru ama okuyarak kesinleşmedi, **verifier-found** = doğrulayıcının eklediği yeni bulgu. Çürütülenler (refuted) listede yok. Bulgu metinleri ajan çıktısı olduğu için İngilizce.


## Core (matematik, RNG, örnekleme, thread) `core`

Most of the core math is correct and textbook-quality: the Duff 2017 orthonormal basis, the cofactor 4x4 inverse, inverse-transpose normal transforms, Arvo AABB transform, PCG32 XSH-RR, and the sphere/hemisphere/concentric-disk/cosine/triangle warps with matching pdfs. The worst defect has its root in this scope: utils.h refractVec implements pbrt's formula, which expects wi pointing away from the surface, but its doc says 'pointing toward'. Dielectric follows the doc and passes -wo, so every refracted direction comes out mirrored in the tangent plane. Curved glass renders wrong, and the only test compares refractVec with itself. Second: StratifiedSampler picks strata from sample index + dimension only, the same for every pixel, so progressive previews are coherently biased. With 2 lights, the first half of the passes pick only light 0. Threading is a single mutex FIFO with a packaged_ta

- **[core-1]** critical · confirmed · `src/core/math/utils.h:154` — refractVec's convention contradicts its doc; Dielectric follows the doc, so all refraction is mirrored in the tangent plane
  - Düzeltme: Pass woLocal (pointing away) at dielectric.cpp:141/167, or rewrite refractVec to match its documented 'toward' convention, and fix the doc either way. Add a Snell test that does not call refractVec: check |wt x n| = eta*|wo x n| and dot(wt_tangent, wo_tangent) < 0.
- **[core-2]** high · confirmed · `src/samplers/stratified_sampler.cpp:22` — StratifiedSampler strata depend only on (sampleIndex + dim), the same for every pixel, so progressive previews are coherently biased
  - Düzeltme: Short term: use a per-pixel random permutation per dimension (pbrt-v3 Shuffle seeded from a hash of pixel and dim) or Cranley-Patterson rotation per pixel and dim. Proper fix: the Owen-scrambled Sobol already planned at ROADMAP:314, indexed by (pixel hash, sampleIndex, dim).
- **[core-3]** medium · confirmed · `src/core/threading/parallel.h:55` — Renders cannot be cancelled; closing the app joins a running full render
  - Düzeltme: Pass a `const std::atomic<bool>* cancel` into parallelFor2D: tiles return early when it is set and the submit loop stops queueing. Have renderProgressive's callback return false to abort. Set the flag in ~Application and from a 'Durdur' button. This is the `cancel` hook RenderDevice already plans (ROADMAP:113).
- **[core-4]** medium · confirmed · `src/core/threading/thread_pool.cpp:35` — Lost-wakeup race in ThreadPool shutdown can hang join(); per-pass pool creation makes it more likely
  - Düzeltme: Wrap the store in `{ std::lock_guard lk(m_mutex); m_stop = true; }` before notify_all. Do the same for the m_activeTasks decrement. Create the override pool once per render, not once per pass.
- **[core-6]** medium · confirmed · `src/core/color/spectrum.h:69` — No NaN/Inf guard anywhere on the sample path; the only isnan check is never called and -ffast-math disables it on GCC/Clang
  - Düzeltme: Move the bit-test finiteFloat into core/math/utils.h, call it once in samplePixel (renderer.cpp:26-38) to reject or zero non-finite or negative samples and count them in debug builds, and delete the two loader copies. Optionally drop -ffinite-math-only by adding -fno-finite-math-only.
- **[core-7]** medium · confirmed · `src/core/math/aabb.h:54` — Slab test recomputes 1/d per axis at every node; the BVH computes invDir and throws it away
  - Düzeltme: Add `bool intersectP(const Ray&, const Vec3f& invDir, const int dirIsNeg[3], float tMax)` that selects pMin/pMax by dirIsNeg (no swap, no divide) and applies the gamma-3 factor to tFar, and use it from both BVH loops.
- **[core-8]** medium · confirmed · `src/core/math/ray.h:21` — Fixed absolute ray epsilons (1e-4) depend on scene scale
  - Düzeltme: Implement the Ray Tracing Gems ch. 6 offset_ray (integer-ulp offset along ng) in core/math, use tMin = 0 with it, and remove the duplicated magic constants.
- **[core-m1]** medium · verifier-found · `src/app/application.cpp:609` — Preview thread holds imageMutex for the entire, uninterruptible sample pass, so UI-thread scene edits and exports block for a full pass
  - Düzeltme: Render into a pass-local buffer without the lock, or give renderSamplePass an atomic cancel/epoch flag checked per tile. Hold the mutex only to swap or merge the result and to swap the scene pointer.
- **[core-5]** low · confirmed · `src/core/threading/parallel.h:86` — An exception in one tile unwinds parallelFor2D while other tiles still use the caller's stack
  - Düzeltme: Catch inside the task wrapper and store the first exception_ptr, set a cancel flag, wait for all futures, then rethrow once. Or adopt a structured parallel_for (TBB / std::execution) that already does this.
- **[core-9]** low · confirmed · `CMakeLists.txt:53` — AVX2/FMA is only a global compiler flag; the code has no SIMD, and the binary needs an AVX2 CPU
  - Düzeltme: Get SIMD where it pays: Embree 4 (ROADMAP:363) or a BVH4 node test with __m128. Keep the default build at SSE4.2/AVX and dispatch AVX2 kernels at runtime if needed.
- **[core-10]** low · confirmed · `src/core/math/quaternion.h:125` — Quaternion::slerp uses na.w*na.w in the dot product; fromEuler axes don't match the engine's Y-up frame
  - Düzeltme: Fix the dot product, rewrite fromEuler for Y-up yaw (about Y), and add tests: slerp(q,q,t) == q, slerp at t=0.5 between identity and a 90-degree Y rotation equals a 45-degree Y rotation, and toMatrix()*v == rotateVector(v).
- **[core-11]** low · confirmed · `src/core/random/rng.h:27` — RNG::uniformFloat can return exactly 1.0
  - Düzeltme: `return float(uniformUint32() >> 8) * 0x1p-24f;` returns values in [0, 1 - 2^-24], exactly representable.
- **[core-12]** low · plausible · `src/samplers/stratified_sampler.cpp:50` — Per-pixel/per-sample streams come from unhashed linear seeds that only change the PCG increment
  - Düzeltme: Seed via a strong hash such as `RNG(hash(px, py, seed))`, then `advance(sampleIndex * 65536)` (pbrt-v4 style, adding PCG's advance()). Better: replace with the index-based Sobol in ROADMAP:314, which also satisfies the determinism item at ROADMAP:275.
  - Not: The verifiable parts hold: m_inc=(seed<<1)|1 with a fixed initstate (rng.h:12-14), startPixel's RNG is immediately replaced by startSample's (renderer.cpp:29-30, stratified_sampler.cpp:42/54), and seeds include the tile origin (renderer.cpp:283-285, used as m_baseSeed through clone, stratified_sampler.cpp:31), so the output depends on tileSize. The claim of correlated streams is not demonstrated.
- **[core-13]** low · confirmed · `src/core/math/utils.h:70` — solveQuadratic loses precision for small or distant spheres and can return 0/0
  - Düzeltme: In Sphere::intersect, use pbrt-v4's form `disc = 4a*(r^2 - |o - (b/2a) d|^2)`, or evaluate a, b, c and the discriminant in double (two lines). Guard q == 0.
- **[core-14]** low · confirmed · `src/core/memory/arena_allocator.cpp:91` — Dead allocator, Quaternion and parallelFor code; ArenaAllocator::reset never reuses blocks 1..n
  - Düzeltme: Delete the unused files and APIs (ladder rung 1). If a per-thread MemoryArena is needed later for BSDF allocation, rewrite reset() to walk the existing blocks.
- **[core-15]** low · confirmed · `src/core/math/mat.h:133` — Degenerate transforms fail silently, and the perspective comment states the wrong depth range
  - Düzeltme: Clamp scale magnitudes to >= 1e-6 at the project_io and inspector boundaries, return a success flag from inverse(), and correct the comment.
- **[core-m2]** low · verifier-found · `src/app/application.cpp:1134` — Full render copies the whole accumulation Image on every spp pass
  - Düzeltme: Copy only on the final pass (spp == target), or have renderProgressive return the final Image and pass a progress-only callback.

## Geometri (BVH, mesh, üçgen) `geometry`

The geometry layer is a pbrt-v3-style single-level BVH. It uses a 12-bin SAH build, 32-byte flattened nodes (left child stored next to its parent), ordered traversal by ray-direction sign, a growable thread_local stack, and a separate early-exit intersectAny for shadow rays. Meshes are not split into heap Triangle objects: PrimRef points to (mesh, triangle index). That is a good base, but it is not fast and it has scale-dependent correctness bugs.

- **[geometry-1]** critical · confirmed · `src/geometry/triangle.cpp:12` — Absolute 1e-6 determinant epsilon in Möller–Trumbore makes small triangles invisible
  - Düzeltme: Reject only a == 0 (or use a threshold relative to |e1||e2|), or better, replace this with the Woop/Benthin/Wald 2013 watertight test, which needs no epsilon and also fixes leaks along shared edges. Add a test: a 1e-4-scale triangle hit at 60° must report a hit.
- **[geometry-2]** high · confirmed · `src/geometry/mesh.cpp:71` — Each candidate triangle test builds a 112-byte Triangle and computes full shading data, even for shadow rays
  - Düzeltme: Store a precomputed triangle array in BVH leaf order (v0, e1, e2 as 36 B, or 48 B Woop data) plus a primId. Make the inner test return only (t, u, v, primId). Build the SurfaceInteraction once after traversal from primId and barycentrics. intersectAny should use the same lean test.
- **[geometry-4]** high · confirmed · `src/geometry/surface_interaction.h:26` — Front/back side is decided by the interpolated shading normal; ng is never made consistent with it; ray offsets use the shading normal
  - Düzeltme: Decide frontFace from ng. When the mesh has vertex normals, flip ng into the hemisphere of ns (pbrt: `ng = FaceForward(ng, ns)`). Flip ns to ng's side. Offset ray origins along ng, not along the shading normal. Optionally add a shadow-terminator fix (Chiang 2019 or Hanika 2021).
- **[geometry-6]** high · confirmed · `src/engine/scene.cpp:52` — No two-level BVH: every edit re-bakes and rebuilds everything, and each area light triggers another full rebuild
  - Düzeltme: Remove buildAccelerator() from addLight and build once after all lights are added. Build one BLAS per TriangleMesh at import and keep it. Add a TLAS over instances {BLAS*, transform, inverse transform}: a transform edit rebuilds only the TLAS (O(I log I)), and material or HDRI changes rebuild nothing. This matches ROADMAP.md:365-369 and 538.
- **[geometry-m1]** high · verifier-found · `src/scene/scene_graph.cpp:50` — Mirrored (negative-determinant) transforms flip winding but not normals, so ng points inward and glass enter/exit inverts
  - Düzeltme: When the transform determinant is negative, swap two indices per triangle in bakeMesh and the glTF loader, or flip ng toward the interpolated normal in Triangle::intersect.
- **[geometry-3]** medium · confirmed · `src/core/math/aabb.h:54` — Slab test recomputes 3 reciprocals per node; the BVH's precomputed invDir is used only for sign
  - Düzeltme: Add AABB::intersectP(const Vec3f& o, const Vec3f& invDir, const int dirIsNeg[3], float tMax), pbrt-v3 style: select pMin or pMax by dirIsNeg, no divisions, no swaps. pbrt also scales tMax by (1 + 2·gamma(3)) for a conservative test. Pass the invDir that the BVH already computes.
- **[geometry-5]** medium · confirmed · `src/integrators/path_tracer.cpp:19` — Fixed 1e-4 self-intersection offset and tMin, with hit points recomputed as ray.at(t): artifacts depend on scene scale
  - Düzeltme: Rebuild the triangle hit point as w·p0 + u·p1 + v·p2. Then either carry pbrt-v4 floating-point error bounds and use OffsetRayOrigin, or apply the Wächter–Binder (Ray Tracing Gems ch. 6) integer-ulp offset along ng, with tMin = 0. Add the 0.01x/100x scale relMSE test from the ROADMAP.
- **[geometry-8]** medium · confirmed · `src/geometry/surface_interaction.h:14` — SurfaceInteraction has no primitive id, barycentrics, dpdu/dpdv or differentials, so emissive hits need linear scans and textures cannot be filtered
  - Düzeltme: Add `uint32_t primId; const Shape* shape/geomId; Vec2f bary; Vec3f dpdu, dpdv;` filled once after traversal (see geometry-2). Map light lookup from geomId to a Light* and pdf via the triangle area CDF. Add ray differentials for texture LOD later.
- **[geometry-9]** medium · confirmed · `src/geometry/sphere.cpp:53` — Sphere tangent has the wrong sign on its x component and can come out anti-parallel to the normal
  - Düzeltme: Use `Vec3f(n.z, 0.0f, -n.x)` (the 2π factor is irrelevant after normalization). Add a test that tangent·normal ≈ 0 at random points. Scale the radius by the uniform part of the world transform, or treat spheres as instanced unit spheres.
- **[geometry-10]** medium · confirmed · `src/geometry/triangle.cpp:66` — Tangents are per-face from UV edges, with no stored per-vertex tangent or handedness; glTF TANGENT is ignored
  - Düzeltme: Store Vec4f tangents per vertex (xyz + sign). Read glTF TANGENT when present, otherwise generate with MikkTSpace. Interpolate them with barycentrics like normals, and use the sign to build B.
- **[geometry-11]** medium · confirmed · `src/geometry/mesh.cpp:11` — Loaders de-index meshes and SceneGraph keeps a second baked copy: about 270 B per triangle resident, with 3 dependent loads per triangle test
  - Düzeltme: Weld vertices on load (hash on position/normal/uv). Move the vectors into the mesh (pass by value + std::move). Share immutable UV and index buffers between source and baked meshes, or stop baking and use instance transforms (geometry-6). The BVH should own a compact leaf-ordered triangle array (geometry-2).
- **[geometry-12]** medium · confirmed · `src/geometry/bvh.cpp:286` — Shadow-ray any-hit path has no alpha or cutout support and no way to skip the light's own primitive
  - Düzeltme: Once primId exists (geometry-8), let intersectAny take an ignore-geomId parameter and an optional alpha-test callback that does a UV lookup only for materials with an alpha map. Then remove the slop.
- **[geometry-m2]** medium · verifier-found · `src/materials/dielectric.cpp:124` — Dielectric ignores the interpolated shading normal, so smooth-shaded glass meshes render faceted
  - Düzeltme: Use ng only to decide entering, and build the reflect/refract frame from ns flipped into ng's hemisphere.
- **[geometry-7]** low · confirmed · `src/geometry/bvh.cpp:219` — A nested BVH::intersect clears the shared thread_local traversal stack, and BVH derives from Shape, which invites exactly that nesting
  - Düzeltme: Use a local fixed-size `int stack[64]` and guarantee a depth limit in the build (for example, fall back to a median split when depth exceeds 60), or pass a stack object per call. Write the TLAS as its own type rather than reusing Shape.
- **[geometry-13]** low · confirmed · `src/geometry/bvh.cpp:138` — BVH build is single-threaded, allocates every node with new, and uses SAH costs that allow 255-primitive leaves of expensive tests
  - Düzeltme: Use prefix and suffix sweeps over the buckets. Allocate nodes from a pool or directly in the flat array. Build subtrees above about 64k prims in parallel on the existing ThreadPool. Tune traversal cost to about 0.125–0.5 once triangle tests are cheap, and cap leaves at 4–8. Delete LinearBVHNode.
- **[geometry-14]** low · confirmed · `src/geometry/bvh.cpp:250` — Children are ordered only by the sign of the split axis; the far child is always pushed and re-tested on pop
  - Düzeltme: Bikker / Aila–Laine style: at an interior node, test both child boxes, descend into the nearer hit, push the farther only if it was hit, and push its tNear so it can be skipped on pop when tNear > ray.tMax. Use a fixed-size array stack.
- **[geometry-15]** low · confirmed · `tests/test_bvh.cpp:12` — Tests do not cover intersectAny, compare against brute force on random data, or test degenerate and scale cases
  - Düzeltme: Add one randomized test: 10k random triangles and spheres, 10k random rays, check that BVH.intersect t equals the brute-force minimum and that intersectAny equals (brute-force hit within tMax). Add axis-parallel rays and a 1e-4-scale copy of the scene.

## Malzemeler (BSDF) `materials`

I read all 14 in-scope files in full and traced the callers: path_tracer.cpp, utils.h (Fresnel and refraction helpers), surface_interaction.h, triangle.cpp, image_io.cpp/image.cpp, gltf_loader.cpp, material_library.cpp and application.cpp.

- **[materials-m1]** critical · verifier-found · `src/materials/dielectric.cpp:141` — Glass refraction direction is mirrored about the normal: refractVec gets -wo but implements PBRT's formula that expects wo, so all glass acts like a negative-index material
  - Düzeltme: Call refractVec(woLocal, n_facing_wo, eta, wi) in both places, or fix the doc and formula in utils.h together. Then add a test that compares against a hand-computed Snell direction, e.g. (0.076, 0.997, 0) for the existing sphere case.
- **[materials-1]** high · confirmed · `src/materials/disney.cpp:218` — Normal, roughness and metalness maps are decoded as sRGB, so a flat normal map tilts surfaces by about 39°
  - Düzeltme: Add a bool srgb parameter to loadMap / loadImageLDR. Linearize only the albedo slot; load normal, roughness and metalness raw. Add the ROADMAP test (an sRGB 0.5 texel reads 0.214 for albedo and 0.502 for data maps).
- **[materials-2]** high · confirmed · `src/materials/dielectric.cpp:67` — Rough glass: reflection inside the glass always evaluates to black, so internal reflection and TIR paths are killed
  - Düzeltme: Follow PBRT v4 DielectricBxDF::f. Face-forward h to +z and reject only if `dot(wo,h)*wo.z <= 0 || dot(wi,h)*wi.z <= 0`. Compute `F = fresnelDielectric(dot(wo,h), 1, ior)` with the +z-oriented h, so the sign carries enter/exit and gives microfacet TIR. Add a test: rough sphere interior, wo.z<0, reflected wi → eval > 0.
- **[materials-3]** high · confirmed · `src/materials/dielectric.cpp:78` — Rough transmission uses 1/eta instead of eta in the half-vector and Jacobian (Walter 2007), so frosted glass is biased
  - Düzeltme: Let etap = entering ? ior : 1/ior. Use h = normalize(wo + wi*etap), face-forwarded to +z. Radiance-mode BTDF: ft = tint*D*G*(1-F)*|wi·h||wo·h| / (|cosI||cosO|*(wo·h + etap*wi·h)²). pdf = (1-fr)*D*|h.z| * etap²*|wi·h| / (wo·h + etap*wi·h)². Check with a chi-square test on the sampled directions.
- **[materials-5]** high · confirmed · `src/materials/disney.cpp:293` — Normal-mapped Disney: the BSDF frame uses the perturbed normal but the integrator's cosine uses the unperturbed one
  - Düzeltme: Either fold |wi·n_shading| into the returned value (PBRT v4 returns f·AbsDot(wi,ns)), or have the material write its shading normal back to the integrator before the cosine is applied. Handle wo below the bent normal by bending the normal back toward the geometric normal, or use Schüssler 2017 microfacet normal mapping.
- **[materials-6]** high · confirmed · `src/integrators/path_tracer.cpp:160` — Next-event estimation skips transmission lobes but MIS still down-weights BSDF hits: diffuse transmission and frosted glass lose direct light
  - Düzeltme: Use |wi·n| in NEE when the BSDF has a transmission lobe (needs flags, see materials-12). Alternatively, give BSDF-sampled transmitted hits MIS weight 1 until NEE handles both hemispheres.
- **[materials-7]** high · confirmed · `src/materials/disney.cpp:14` — Isotropic GGX D overflows to inf (and the path to NaN) below roughness ≈ 0.016, and NaNs permanently poison pixels
  - Düzeltme: Use the cancellation-free form D = a2 / (π·(s2 + a2·c2)²) with s2 = h.x²+h.y² and c2 = h.z², or reuse the tan² formulation already in ggxDAniso (disney.cpp:42-54). Drop non-finite samples before accumulating them.
- **[materials-8]** high · confirmed · `src/materials/dielectric.cpp:124` — Glass ignores interpolated shading normals: smooth-shaded glass meshes refract per facet, and throughput is not cos-consistent
  - Düzeltme: Use the outward shading normal n = si.frontFace ? si.normal : -si.normal. SurfaceInteraction::frontFace (surface_interaction.h:22) already encodes enter vs exit, so the original reason for using ng no longer applies.
- **[materials-4]** medium · confirmed · `src/materials/dielectric.cpp:150` — Radiance scaling for refraction is inverted: entering glass multiplies by ior² instead of 1/ior²
  - Düzeltme: Multiply by eta*eta (= (η_wo/η_wi)²) instead of dividing, or adopt the PBRT v4 etap convention. Add a TransportMode argument before adding BDPT or photon mapping.
- **[materials-9]** medium · confirmed · `src/materials/disney.cpp:143` — Disney specular F0 is 0.04·specular instead of Burley's 0.08·specular, so glTF imports get F0 = 0.02
  - Düzeltme: Use 0.08f * p.specular in disney.cpp:143 and material_library.cpp:77. Long term, take F0 from a specular_ior parameter, as OpenPBR does.
- **[materials-10]** medium · confirmed · `src/materials/disney.cpp:154` — Clearcoat is a full-weight GGX lobe added on top of the base without attenuation, which gains energy at grazing angles
  - Düzeltme: Either restore Burley's GTR1 with the 0.25 weight, or follow OpenPBR: scale the base by (1 - coat·E_coat(μ)) (albedo scaling) or at least by (1 - coat·Fc(wo))·(1 - coat·Fc(wi)). Add a furnace test.
- **[materials-11]** medium · confirmed · `src/materials/disney.cpp:274` — Texture channel fallbacks and replace-instead-of-multiply break packed ORM maps and glTF factors
  - Düzeltme: Store an explicit channel per slot (R/G/B/A) and multiply factor × texel, as the glTF spec requires. Interpolate per-vertex tangents with a w sign when the mesh provides them.
- **[materials-13]** medium · confirmed · `src/materials/dielectric.cpp:137` — Glass tint colors reflections and is applied per interface, not by Beer-Lambert absorption over path length
  - Düzeltme: Leave Fresnel reflection untinted. Replace tint with an absorption coefficient σa = -ln(color)/depth, applied as exp(-σa·t) in the integrator while the ray is inside the medium. This needs medium tracking and is a stepping stone to nested dielectrics.
- **[materials-14]** medium · confirmed · `tests/test_dielectric.cpp:44` — No white-furnace, chi-square or reciprocity tests; current material tests are smoke checks that miss materials-2/3/7
  - Düzeltme: Add a tests/test_furnace.cpp that integrates Σ f·|cos|/pdf over 1e6 samples for every material/roughness, expecting ≤1, and ≈1 for white Lambertian and glass. Add a chi-square test of sample() against pdf() on a 2D histogram, as Mitsuba and PBRT do.
- **[materials-12]** low · confirmed · `src/materials/material.h:29` — No sample record: the integrator infers delta lobes by calling eval(), costing two extra evaluations per bounce
  - Düzeltme: Add `struct BSDFSample { Color3f f; Vec3f wi; float pdf; float eta; uint8_t flags; /*Reflection|Transmission|Diffuse|Glossy|Specular*/ };`, plus `virtual uint8_t flags() const` and a separate float uc for lobe choice. The integrator reads flags instead of calling eval().
- **[materials-15]** low · confirmed · `src/materials/disney.cpp:24` — GGX sampling uses the full normal distribution instead of visible normals (VNDF), and diffuse/specular lobe selection is fixed at 50/50
  - Düzeltme: Implement Heitz 2018 VNDF sampling (or Dupuy & Benyoub 2023 spherical caps) with pdf = G1(wo)·max(0,wo·h)·D(h)/(wo.z·4·(wo·h)). Weight lobe selection by approximate albedo, e.g. luminance of the Schlick F0-based spec albedo versus (1-metallic)·baseColor.

## Işıklar ve integratör `lighting`

The core is a unidirectional path tracer with NEE plus BSDF sampling, combined with the power heuristic (path_tracer.cpp:55-211). Point and directional lights are flagged as delta. The rectangle area light is two-sided, sampled uniformly by area and converted to a solid-angle pdf. Mesh lights pick triangles by area. The equirect environment light uses a luminance*sin(theta) marginal/conditional CDF. sampleLi pdfs are solid-angle, and pdfLi matches sampleLi for area and env lights (tests check this). MIS is internally consistent, so the estimator is unbiased for opaque surfaces. The weights still omit the light-selection pmf and the shadow-sample count (PBRT v4 includes both). Light selection is uniform and Light::power() has no callers.

- **[lighting-1]** critical · confirmed · `src/lights/environment_light.h:51` — Data race / use-after-free on the HDRI image while the progressive render runs
  - Düzeltme: Have EnvironmentLight own an immutable std::shared_ptr<const Image>. Load and build the new Image and EnvironmentLight off the shared state, then swap the shared_ptr inside the imageMutex section of rebuildScene. Never mutate m_state.envMap in place.
- **[lighting-2]** high · confirmed · `src/engine/scene.cpp:36` — Area-light intensity slider desyncs NEE radiance from the visible/BSDF-hit emitter
  - Düzeltme: Use a single source of truth. Either the emissive material reads radiance from the AreaLight pointer, or setRadiance is only called through rebuildScene() under imageMutex.
- **[lighting-3]** high · confirmed · `src/integrators/path_tracer.cpp:160` — NEE discards transmitted light while BSDF-side MIS still down-weights it (energy loss through rough glass / translucent Disney)
  - Düzeltme: Use `float cosTheta = std::abs(ls.wi.dot(isect.normal));` and keep only the `!brdf.isBlack()` check. Add a furnace test with a frosted-glass sphere.
- **[lighting-4]** high · confirmed · `src/integrators/path_tracer.cpp:113` — Non-physical contact-AO multiplier is enabled by default and scale-dependent
  - Düzeltme: Remove AO from the beauty pass and set the default to 0. If an AO look is wanted, offer it as a separate AOV/post layer.
- **[lighting-5]** high · confirmed · `src/integrators/path_tracer.cpp:191` — Glass throughput scaled by |wi·ns|/|wi·ng| on smooth-shaded meshes (fireflies on bottle silhouettes)
  - Düzeltme: Have sample() return the weight f*|cos|/pdf, as in PBRT v4 BSDFSample, or use the same normal in the material and the integrator. Do not apply an external cosine to delta lobes.
- **[lighting-7]** high · confirmed · `src/lights/area_light.cpp:129` — MeshLight::pdfLi is a linear scan over all triangles on every emissive BSDF hit
  - Düzeltme: Record the Light* and primitive index in SurfaceInteraction at intersection time (mesh.cpp intersectTriangle already knows the index). pdfLi then becomes O(1). Use clamp(sample.x, 0, 1-ulp) instead of 0.999.
- **[lighting-10]** high · confirmed · `src/integrators/path_tracer.cpp:210` — No NaN/Inf guard or firefly clamp; one bad sample poisons a progressive pixel
  - Düzeltme: Drop samples that fail !isValid() or isinf (count them for debugging). Add an optional clamp on indirect contribution (off by default), and clamp alpha to at least ~1e-4 in the GGX evaluation.
- **[lighting-6]** medium · plausible · `src/integrators/path_tracer.cpp:19` — Fixed 1e-4 world-space ray offset along the shading normal: acne at large coordinates, leaks at the shading terminator
  - Düzeltme: Use the Wächter-Binder (Ray Tracing Gems ch. 6) offset along isect.ng, scaled by max|p|, and set tMin to 0 for offset origins.
  - Not: The fixed 1e-4 offset along the shading normal (path_tracer.cpp:18-20) plus tMin = 1e-4 (ray.h:21) is only ~1.6 ulp at Cornell coordinates around 556 (application.cpp:473-482). Möller-Trumbore t error there can exceed 1e-4 at grazing angles (triangle.cpp:17-33), so acne is likely, but I have not confirmed it visually. The 'leak at the terminator' part is overstated: when dir·ns > 0 but dir·ng < 0, the ray re-hits its own triangle, which darkens rather than leaks; the roadmap item at ROADMAP.md:325 is real.
- **[lighting-8]** medium · confirmed · `src/integrators/path_tracer.cpp:122` — Uniform light selection, unused power(), and emissive spheres never get NEE
  - Düzeltme: Fix power() for each light type, build an alias table or a CDF over power (pmf passed into the MIS weights), and register every emissive shape. Later move to a light BVH (ROADMAP.md:319).
- **[lighting-9]** medium · confirmed · `src/integrators/path_tracer.cpp:167` — MIS weights omit light-selection pmf and NEE sample count (PBRT v4 mismatch)
  - Düzeltme: Use p_l = pmf(light) * ls.pdf in both places, and pass powerHeuristic(shadowSamples, p_l, 1, p_b) and its complement.
- **[lighting-11]** medium · confirmed · `src/integrators/path_tracer.cpp:202` — Russian roulette uses luminance with a 0.95 cap and a hard-coded start depth
  - Düzeltme: Use q = max(0, 1 - maxComponent(throughput * etaScale)), apply only when that value is < 1 and depth > minDepth, and expose minDepth in RenderSettings (ROADMAP.md:327).
- **[lighting-12]** medium · confirmed · `src/engine/scene.cpp:52` — Every AreaLight triggers a full BVH rebuild on each scene edit
  - Düzeltme: Add the light quads as shapes and call buildAccelerator() once at the end of rebuildScene, or make it lazy with a dirty flag.
- **[lighting-13]** medium · confirmed · `src/app/application.cpp:824` — HDRI rotation/intensity never exposed; background is always the lighting HDRI
  - Düzeltme: Add setRotation/setIntensity (rotation needs no CDF rebuild), wire them to UI sliders and the GL preview, and add a separate background mode (HDRI / color / transparent) used only when depth == 0.
- **[lighting-14]** medium · confirmed · `src/lights/environment_light.h:20` — Equirect mapping is likely horizontally mirrored relative to Mitsuba/three.js conventions
  - Düzeltme: Render an HDRI with readable text, or with the sun at a known azimuth, and compare against Blender/Mitsuba. If it is mirrored, change the sign in directionToEquirect and equirectToDirection (environment_light.cpp:13-15) together.
- **[lighting-15]** medium · confirmed · `src/integrators/path_tracer.cpp:117` — Specular detection inferred from zero BRDF value; up to 5 material resolves per vertex; Light interface lacks pdfLi/L
  - Düzeltme: Add Material::flags() (Delta/Glossy/Diffuse/Transmission), resolve a per-hit BSDF object once, add virtual Light::pdfLi(ctx, wi) and Light::L(isect, w), and store Light* in SurfaceInteraction.
- **[lighting-m1]** medium · verifier-found · `src/integrators/path_tracer.cpp:70` — Last vertex does MIS-weighted NEE but its BSDF-sampled complement is never traced (energy loss at max depth)
  - Düzeltme: Skip NEE on the final iteration (if depth == m_maxDepth-1, add emission and break before the NEE block), or trace one more intersection for the emitter/env MIS term.
- **[lighting-m2]** medium · verifier-found · `src/preview/gl_preview.cpp:643` — GL preview environment cube never refreshes when switching between same-size environments
  - Düzeltme: Add an environment generation counter, bumped in applyHdrEnvironment/applyEnvironmentEntry, and include it in the cache key.
- **[lighting-16]** low · confirmed · `tests/test_mesh_light.cpp:36` — Light/integrator tests check self-consistency only; no furnace or convergence test
  - Düzeltme: Add furnace tests with IndependentSampler at N≈4096 for Lambertian, rough dielectric and Disney transmission. Add a MeshLight pdfLi-vs-sampleLi test and a two-light power-selection test (ROADMAP.md:320).
- **[lighting-m3]** low · verifier-found · `src/core/image/image.cpp:41` — Environment lookup wraps V across the poles (zenith blends with nadir)
  - Düzeltme: Clamp y0/y1 to [0, h-1] for equirect lookups and wrap only u (e.g., add a sampleEquirectBilinear helper).

## Motor, kamera, örnekleyici, denoiser, CLI `engine`

The engine is a small, readable CPU tile renderer. renderer.cpp has two paths. render() is tile-major: all SPP for a tile, then the next tile; the CLI and turntable use it. renderSamplePass()/renderProgressive() are pass-major: one sample over every pixel per pass; the preview and the full-render dialog use them. Both go through one global mutex-queue ThreadPool. There are no per-sample heap allocations and no shared_ptr copies in the hot loop. But five of the most important problems sit in this scope or right at its edges, and none of them is in the docs. (1) PNG and EXR export write the raw per-pixel sample SUM, not the average, so images come out SPP times too bright. They are only correct when the denoiser has already collapsed the count to 1. They are also written upside-down compared with the viewport. (2) StratifiedSampler gives dimension d the stratum (i+d) mod n with no per-dime

- **[engine-1]** critical · confirmed · `src/core/image/image_io.cpp:46` — PNG/EXR export writes the raw sample SUM, not the average: SPP-times over-exposed
  - Düzeltme: Use getAveragedPixel in both writers (or add an Image::resolved() helper that divides by count). Add a test: render a constant-emission scene at 16 SPP, save the PNG, reload it, and expect the same value as at 1 SPP.
- **[engine-2]** high · confirmed · `src/engine/renderer.cpp:34` — Exported images are vertically flipped compared with the viewport
  - Düzeltme: Settle on one raster convention: y=0 at the top, so v = 1 - (y+jy)/h in samplePixel and fillPrimaryAovs. Then remove the UV flip in application.cpp:1908 and viewport_texture.cpp:61-64. Add a golden test with an asymmetric scene.
- **[engine-3]** high · confirmed · `src/samplers/stratified_sampler.cpp:22` — StratifiedSampler locks consecutive dimensions together (fixed cyclic shift, no shuffle), so multi-bounce GI is biased
  - Düzeltme: Give each dimension its own per-pixel random permutation of strata (PBRT-v3 StratifiedSampler shuffle, or a hashed permutation of (pixel, dim)). Better: switch to Owen-scrambled Sobol (ROADMAP F1:314). Add a test: a 2-bounce furnace or a product integrand f(u3)·g(u5) compared against IndependentSampler.
- **[engine-4]** high · confirmed · `src/engine/renderer.cpp:292` — Progressive preview: every pixel uses the same strata at pass k, so early frames are structured and biased
  - Düzeltme: Decorrelate pixels: add a per-pixel Cranley-Patterson rotation or a per-pixel stratum permutation keyed on hash(x,y,dim), or use Sobol with per-pixel Owen scrambling and index shuffling (Burley 2020). Also build the stratified grid from the pass count, not samplesPerPixel.
- **[engine-5]** high · confirmed · `src/app/application.cpp:609` — UI frame rate is capped at the CPU sample-pass rate: imageMutex is held for a whole pass
  - Düzeltme: Render into an accumulator the render thread owns. Publish a display copy through a double or triple buffer with a short lock, or an atomic pointer swap. Hand scenes over as shared_ptr<const Scene> plus a generation number, so rebuildScene never waits for a pass.
- **[engine-6]** high · confirmed · `src/engine/renderer.cpp:288` — No cancellation anywhere: camera moves wait for the current pass, full renders cannot be stopped, app exit hangs
  - Düzeltme: Pass a std::stop_token or an atomic generation into renderSamplePass/renderProgressive. Check it per tile (or per row) and return early. Throw away a pass whose generation is stale. Add a Stop button and check the token in renderProgressive's loop.
- **[engine-7]** high · confirmed · `src/engine/scene.h:49` — Render Scene does not own materials or the env image, so background full render/turntable can read freed memory
  - Düzeltme: Have Scene keep a vector<shared_ptr<const Material>> filled during compile, and make EnvironmentLight hold a shared_ptr<const Image>. Or block scene-mutating actions while fullRender.active.
- **[engine-14]** high · confirmed · `src/engine/renderer.cpp:168` — Adaptive sampling uses absolute, image-global variance and breaks stratification
  - Düzeltme: Use a per-tile relative error, e.g. |I − I_half|/sqrt(I) from two half-buffers, with a minimum SPP and tile-level stopping (ROADMAP F1:328). Keep stratification intact by giving each pixel its own sample index.
- **[engine-m1]** high · verifier-found · `src/app/application.cpp:818` — Environment changes rewrite m_state.envMap on the UI thread while the preview pass is reading it (race / use-after-free)
  - Düzeltme: Build the new env Image into a shared_ptr<Image> that the EnvironmentLight owns, and swap it in under imageMutex inside rebuildScene. Never mutate the Image the live scene is reading.
- **[engine-m2]** high · verifier-found · `src/app/application.cpp:1706` — Inspector material and texture edits mutate the live DisneyMaterial without the lock; texture swaps can free an Image mid-sample
  - Düzeltme: Apply material edits under imageMutex, or give the render scene immutable material snapshots: copy on edit and swap the copy in during rebuildScene.
- **[engine-8]** medium · confirmed · `src/engine/scene.cpp:52` — Every edit, including a material swap, re-bakes all geometry and rebuilds the BVH N+1 times on the UI thread
  - Düzeltme: Build the BVH once after all lights are added (remove scene.cpp:52 and let the caller build). Route materials through an ID table so a material edit only swaps the table entry. Cache baked meshes per node revision.
- **[engine-9]** medium · confirmed · `src/app/application.cpp:1778` — Changing exposure or the tone-map operator throws away all accumulated samples
  - Düzeltme: On TMO/exposure change, set a 'reupload' flag instead of calling markDirty. Keep post-processing out of the accumulation buffer (as ROADMAP F2:347 already says).
- **[engine-10]** medium · confirmed · `src/engine/denoiser.cpp:41` — The non-OIDN 'denoise' is a luminance-weighted 3x3 box blur that removes highlights, yet it is labelled 'OIDN Denoise' and on by default
  - Düzeltme: When !denoiseAvailable(), disable or rename the toggle. Either ship the prebuilt OIDN (ROADMAP F3:359) or write a real edge-aware fallback (à-trous wavelet guided by albedo, normal and variance, SVGF-style). Do not down-weight by absolute luminance.
- **[engine-12]** medium · confirmed · `src/camera/orthographic_camera.h:12` — Focal-length conversion uses 36 mm as the sensor HEIGHT; aperture is a raw world-space radius
  - Düzeltme: Use a 36×24 sensor with sensor-fit (horizontal for landscape, like Blender/KeyShot) and lens radius = focal/(2·N), converted with a scene unit scale. Fix the test value.
- **[engine-13]** medium · confirmed · `src/engine/render_settings.h:24` — Non-physical AO multiplier is on by default in the sample scene; settings and UI are duplicated or missing
  - Düzeltme: Default aoStrength to 0 and label it 'stylised'. Remove the duplicate sliders. Keep preview resolution in its own field. Expose RR depth and seed in advanced mode.
- **[engine-m3]** medium · verifier-found · `src/app/application.cpp:1560` — Area-light intensity slider updates NEE radiance only; the emissive quad keeps the old radiance, so MIS energy is wrong
  - Düzeltme: Call rebuildScene() instead of markDirty() after setRadiance, or have the emissive quad read its radiance from the AreaLight.
- **[engine-11]** low · confirmed · `src/engine/renderer.cpp:77` — OIDN aux buffers are aliased, pinhole-only and wrong for mirrors; device is created per call; viewport denoise runs on the UI thread with no aux
  - Düzeltme: Accumulate albedo and normal inside samplePixel at the first non-delta vertex, using the same jitter and lens sample. Cache the device and filter. Denoise on a worker thread into a separate display buffer.
- **[engine-15]** low · confirmed · `src/main.cpp:52` — CLI is a hard-coded Cornell demo: no arguments, not built by default, writes broken PNG
  - Düzeltme: Do ROADMAP F0:293-295: loadProject() in photon_engine, plus --scene/--spp/--res/--out/--seed/--threads/--denoise and stats.json. It doubles as the benchmark harness.
- **[engine-16]** low · confirmed · `src/samplers/independent_sampler.cpp:26` — IndependentSampler resets its stream on every sample; seeding is linear XOR with no hash; per-pass ThreadPool creation
  - Düzeltme: Make startSample re-seed from hash(pixel, sampleIndex) using splitmix64 or a PCG hash. Keep a persistent ThreadPool in Renderer.

## Görüntü, renk, ton eşleme `color`

The color pipeline is small and mostly readable: float RGB images, exact piecewise sRGB decode for 8-bit textures, exposure in real EV stops, four per-channel global tone-map operators, and new file-size/dimension limits applied before decoding. Two bugs give wrong images in normal use. (1) Every PNG/EXR export path (File->Export, Full Render, Turntable, CLI main.cpp) writes the accumulated SUM of samples instead of the mean, because saveImagePNG/saveImageEXR read getPixel()/data() while the renderer only calls addSample(). With the 128 spp preview default that is +7 EV, so the export is near-white. The viewport looks right because it calls getAveragedPixel(). The image is only correct when the denoiser has run, since it resets counts to 1. (2) Normal, roughness and metalness maps go through the same sRGB->linear decode as albedo. A flat normal map then tilts every normal by about 39 deg

- **[color-1]** critical · confirmed · `src/core/image/image_io.cpp:46` — Every PNG/EXR export writes the SUM of samples, not the mean: images are spp-times too bright
  - Düzeltme: Do it in one place. In saveImagePNG and saveImageEXR, read `img.getSampleCount(x,y) > 0 ? img.getAveragedPixel(x,y) : img.getPixel(x,y)` (the fallback keeps setPixel-built images working). Add one GTest: an Image with 4x addSample(0.18) must produce the same PNG byte as toneMap(0.18).
- **[color-2]** high · confirmed · `src/materials/disney.cpp:218` — Normal, roughness and metalness maps are sRGB-decoded like albedo (flat normal map tilts normals ~39 degrees)
  - Düzeltme: Add `bool srgb = true` to loadImageLDR, loadImageLDRMemory and imageFromRgb8 (skip sRGBToLinear when false). Give loadMap(path, srgb) srgb=false for normal, roughness and metalness. Add the ROADMAP F2:339 test for both cases: color 0.5 -> 0.214, data 0.5 -> 0.502.
- **[color-3]** high · confirmed · `src/core/image/image.cpp:44` — OBJ textures render vertically flipped (v=0 maps to the top row)
  - Düzeltme: One line in obj_loader.cpp:180: `Vec2f(u, 1.0f - v)`. Leave sampleBilinear and glTF alone.
- **[color-4]** high · confirmed · `src/app/application.cpp:1778` — Changing exposure or the tone-map operator discards all accumulated samples
  - Düzeltme: Replace both markDirty() calls with `m_state.imageReady = true;` so only the viewport texture is re-uploaded. Longer term, tone-map in a shader (see perf).
- **[color-m1]** high · verifier-found · `src/core/image/image_io.cpp:57` — PNG and EXR exports are vertically flipped relative to the viewport
  - Düzeltme: Call stbi_flip_vertically_on_write(1) once, or write rows h-1..0 in saveImagePNG and saveImageEXR. Add a test that renders a scene with a known top/bottom asymmetry and checks the exported row order.
- **[color-m2]** high · verifier-found · `src/app/application.cpp:818` — Loading an HDR environment resizes the shared envMap while render threads are reading it (data race / use-after-free)
  - Düzeltme: Give each EnvironmentLight its own std::shared_ptr<const Image> (build the new Image, then swap the pointer under imageMutex) instead of mutating m_state.envMap in place.
- **[color-5]** medium · confirmed · `src/core/image/tone_mapping.cpp:84` — Display encode is pow(1/2.2), not the sRGB OETF; asymmetric with the exact sRGB decode
  - Düzeltme: In toneMap, return `ldr.clamp().linearToSRGB()` and change both GLSL pows to the piecewise sRGB formula. Delete applyGamma/gammaCorrect if nothing else uses them.
- **[color-6]** medium · confirmed · `src/core/image/image.cpp:72` — No NaN/Inf/negative guard: one bad sample poisons a pixel for the whole accumulation, and the quantize step is UB
  - Düzeltme: In addSample: `if (!std::isfinite(c.r) || !std::isfinite(c.g) || !std::isfinite(c.b)) return;`. In toneMap: clamp `exposed` to >= 0 before the operator.
- **[color-7]** medium · confirmed · `src/core/image/tone_mapping.cpp:23` — Tone operators not matched at mid-grey; ACES is a per-channel Narkowicz fit, not RRT/ODT
  - Düzeltme: Add Khronos PBR Neutral (about 15 lines, keeps product color) and AgX as enum values. Normalize the operators so 0.18 lands near the same output, or document the difference. Pass tmo to the GL preview shaders.
- **[color-9]** medium · confirmed · `src/app/application.cpp:1093` — Viewport is not WYSIWYG: export skips the denoised image, and GPU->CPU crossfade mixes operators
  - Düzeltme: Keep the last displayed Image (denoised or not) and export that. Pass settings.tmo to the GL preview.
- **[color-10]** medium · confirmed · `src/preview/viewport_texture.cpp:39` — Viewport tone-maps on the CPU, on the UI thread, while holding the render mutex
  - Düzeltme: Upload linear float (GL_RGB16F/32F via glTexSubImage2D after a /count normalize) and run exposure, TMO, OETF and dither in a fragment shader. Interim fix: copy accumImage under the lock, then tone-map after unlocking.
- **[color-11]** medium · confirmed · `src/core/image/image_io.cpp:119` — EXR load/save fails for non-ASCII (Turkish) paths: ANSI dialog paths reach tinyexr's UTF-8 open
  - Düzeltme: Make the whole process UTF-8: add `<activeCodePage>UTF-8</activeCodePage>` to the app manifest and define STBI_WINDOWS_UTF8. A-APIs, std::filesystem::path(std::string), stb and tinyexr then all agree.
- **[color-12]** medium · confirmed · `src/core/image/image.h:40` — No alpha anywhere: loaders drop it, writers cannot write it
  - Düzeltme: Add an optional alpha plane: coverage from primary rays in the renderer, base-color alpha in textures. Write 4-channel PNG and EXR with premultiplied alpha.
- **[color-13]** medium · confirmed · `src/core/image/image_io.cpp:23` — Textures stored at 16 B/texel (float RGB + unused sample count); limit allows 4.3 GB per texture
  - Düzeltme: Allocate m_sampleCounts lazily (only in addSample), or keep 8-bit textures as RGBA8 with a 256-entry sRGB->linear LUT at sample time. Lower kMaxImageDim to 8192, or add a total-texel budget.
- **[color-14]** medium · confirmed · `tests/test_color.cpp:33` — No tests for transfer functions, tone mapping, or export brightness
  - Düzeltme: Add one test file: sRGB round-trip at 0, 0.04045, 0.5, 1; toneMap mid-grey and white per operator; an addSample x4 -> saveImagePNG -> stbi_load pixel check; data-texture raw load.
- **[color-m3]** medium · verifier-found · `src/app/application.cpp:2139` — UI thread blocks for a whole sample pass on every viewport refresh
  - Düzeltme: Render each pass into a private buffer, or double-buffer accumImage. Hold the mutex only for a swap or copy, and have the UI tone-map a snapshot.
- **[color-m4]** medium · verifier-found · `src/core/image/image_io.cpp:27` — Drag-and-drop paths are UTF-8 but the image loaders treat narrow strings as ANSI
  - Düzeltme: Pick one internal path encoding (UTF-8). Switch the dialogs to the W APIs, define STBI_WINDOWS_UTF8, and build paths with std::filesystem::u8path or a char8_t conversion.
- **[color-8]** low · confirmed · `src/core/image/image_io.cpp:50` — 8-bit quantization truncates with no rounding or dither: -0.5 LSB bias and banding
  - Düzeltme: Factor out one `uint8_t quantize(float v, float noise)` = clamp(v*255 + noise + 0.5). Use a triangular or blue-noise dither of +/-1 LSB. Share it between export and viewport.
- **[color-15]** low · confirmed · `src/materials/disney.cpp:218` — LDR loader accepts .hdr (clipped to 8-bit) and HDR loader accepts PNG with pow-2.2 decode
  - Düzeltme: In loadMap, check `stbi_is_hdr(path)` first and use loadImageHDR. In loadImageHDR, reject !stbi_is_hdr inputs, or route them through loadImageLDR.
- **[color-16]** low · confirmed · `src/core/image/tone_mapping.h:37` — API defaults and docs disagree with EV semantics; EXR is written uncompressed
  - Düzeltme: Set the defaults to 0.0f and fix the comment. Set `header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP` (and HALF pixel types for beauty). Delete draw() and gpuTexture.

## İçe aktarma, sahne grafiği, proje dosyası `io`

The loaders are hardened well against bad input: size caps, index checks, finite-float bit tests, accessor byte-range checks and node-cycle checks, all covered by tests. glTF node transforms are correct. The problems sit around the loaders and they are serious. (1) A glTF primitive with no material receives a non-owning shared_ptr to the app's local default material. That pointer dangles as soon as importModelInternal returns, so rendering it is a use-after-free. (2) The compiled Scene holds raw Material pointers, but the UI thread clears or replaces graph materials without taking the render mutex. (3) Studio presets are parsed by cutting the "lights" array at the first ']', which is the closing bracket of the first light's position. "Product Studio" therefore clears all lights and adds none. (4) .photon files save only DisneyMaterial base parameters, plus the import list and transforms.

- **[io-1]** critical · confirmed · `src/io/gltf_loader.cpp:231` — glTF primitives without a material get a dangling default-material pointer (use-after-free)
  - Düzeltme: Do not alias caller memory in the loader. If prim.material is null, push a null shared_ptr (as ObjLoadResult already does, obj_loader.h:17) and let Application substitute its owning defaultMat, the same way the OBJ branch does at application.cpp:684-687. Add a test that loads a material-less glTF with a non-null default, destroys the default, and then reads mesh->material().
- **[io-2]** critical · confirmed · `src/scene/scene_graph.cpp:62` — Compiled Scene holds raw Material* while the UI thread frees graph materials without the render lock
  - Düzeltme: Have compile() record `std::vector<std::shared_ptr<const Material>>` in Scene (next to m_emissiveMaterials, scene.cpp:36-37) so the compiled snapshot owns its materials. Alternatively, take imageMutex around every graph mutation. ROADMAP's 'değişmez sahne anlık görüntüsü' (immutable scene snapshot) describes the same fix.
- **[io-m4]** critical · verifier-found · `src/app/application.cpp:1132` — Full render and turntable threads trace raw Material* with no lock for the whole render
  - Düzeltme: Have the compiled Scene keep shared_ptr<Material> alive (for example a vector<shared_ptr<Material>> filled in compileNode), or block graph mutations while a full render runs.
- **[io-3]** high · confirmed · `src/app/application.cpp:1025` — Studio presets load zero lights: lights array is cut at the first ']' (inside the first light's position)
  - Düzeltme: Match brackets with a depth counter, as parseProjectJson already does for the camera braces (project_io.cpp:244-252), or adopt a real JSON library once (nlohmann/json via vcpkg) for project_io, material_library and studios. Add a test that applies product_studio.json and expects 3 lights.
- **[io-4]** high · confirmed · `src/scene/project_io.cpp:157` — .photon save/load drops lights, glass, anisotropy/sheen/emission, procedural env, visibility and deletions
  - Düzeltme: Serialize a material record by type (disney/dielectric/lambert/mirror) with all parameters plus the preset id, a lights array, the environment entry id, intensity and rotation, plus per-node visible/name. Add the save->load->save identical-JSON test that ROADMAP:293 already plans.
- **[io-5]** high · confirmed · `src/scene/project_io.cpp:272` — Backslashes are escaped on save but never unescaped on load, so Windows paths double every cycle
  - Düzeltme: Unescape \\ and \" in a single readJsonString helper and use it everywhere. Better still, store paths with '/' via path.generic_string(). Add a round-trip test with a backslash path.
- **[io-7]** high · confirmed · `assets/README.md:20` — Sample OBJ models are git-ignored, so the documented sample product scene never loads
  - Düzeltme: Scope the ignore (e.g. '/build*/**/*.obj' or '!assets/**/*.obj'), commit the procedural models (or generate them at startup), and point the sample scene at sample_box.gltf if they are missing.
- **[io-8]** high · confirmed · `src/io/gltf_loader.cpp:197` — glTF material import reads only base color factor/texture and MR factors; texture replaces factor
  - Düzeltme: Map MR texture (G=roughness, B=metal, as disney.cpp:269-274 already expects), normal texture (linear, with scale), emissive factor and texture, occlusion (or ignore it explicitly), and KHR_materials_transmission/ior to Dielectric. Multiply the factor with the texture. Decode data URIs with cgltf_load_buffer_base64 and URIs with cgltf_decode_uri.
- **[io-9]** high · confirmed · `src/io/gltf_loader.cpp:230` — glTF re-creates material and re-decodes texture per primitive at 16 bytes/pixel; 4K = 268 MB per copy
  - Düzeltme: Cache material shared_ptrs per cgltf_material index and textures per cgltf_image index (std::unordered_map). Add a texture type without m_sampleCounts, stored as RGBA8 or half.
- **[io-15]** high · confirmed · `src/scene/project_io.h:10` — Undo/redo closures capture raw SceneNode* that import-undo frees; redo writes freed memory
  - Düzeltme: Reference nodes by stable id (pickId or a uint64 node id) and look them up at execution time, or keep removed subtrees alive inside the undo command instead of destroying them.
- **[io-m1]** high · verifier-found · `src/scene/project_io.cpp:176` — Project reload strips glTF base-color textures (and any setAlbedoImage texture)
  - Düzeltme: Write overrides only for user-edited materials, or record the source texture path/URI on DisneyMaterial when the loader sets the image.
- **[io-m2]** high · verifier-found · `src/materials/disney.cpp:218` — Normal, roughness and metalness maps go through the sRGB-to-linear decode
  - Düzeltme: Add a linear (no-decode) LDR load path and use it for normal, roughness and metalness maps. Keep sRGB decoding for albedo only.
- **[io-m3]** high · verifier-found · `src/app/application.cpp:818` — HDR or procedural environment change resizes envMap without imageMutex while a render pass samples it
  - Düzeltme: Have EnvironmentLight own a shared_ptr<const Image>, and build the new image off to the side before swapping it in under the lock.
- **[io-6]** medium · confirmed · `src/scene/project_io.cpp:344` — Project save is non-atomic and unchecked, goes to CWD, has unversioned format and absolute paths
  - Düzeltme: Write to path+'.tmp', check the stream, then std::filesystem::rename. Return and surface errors. Add Save As. Store paths relative to the project file's directory (std::filesystem::relative) and resolve them against it on load. Reject version > supported.
- **[io-10]** medium · confirmed · `src/scene/scene_graph.cpp:100` — Every edit runs a full compile: all meshes are re-copied and the BVH is rebuilt 1 + (number of area lights) times, even for env/light changes
  - Düzeltme: Split compile into geometry build (only when geometry or transforms change) and cheap updates. Material swap means rebinding a pointer, env/light change means setEnvironment plus markDirty. Add area-light quads before a single build. Longer term, use instancing with a per-mesh BLAS and a TLAS of node transforms, so transform edits only refit the TLAS. Batch imports so there is one rebuild at the end.
- **[io-11]** medium · confirmed · `src/io/obj_loader.cpp:182` — Loaders create a separate vertex per corner, so OBJ without vn renders faceted; mixed vn/vt faces lose all normals and UVs; OBJ UVs not V-flipped
  - Düzeltme: Keep vertices indexed: dedupe OBJ (v,vn,vt) triplets with an unordered_map and use glTF accessors directly. Generate smooth normals per smoothing group or with an angle threshold. Pad missing per-corner normals and UVs so arrays stay aligned. Store 1-v for OBJ.
- **[io-12]** medium · confirmed · `src/io/obj_loader.cpp:204` — MTL maps to Lambertian(Kd) only; the result is neither editable in the UI nor saved
  - Düzeltme: Build DisneyMaterial with baseColor=Kd, roughness from Ns (e.g. sqrt(2/(Ns+2))), specular from Ks, emission=Ke, setAlbedoMap(baseDir/map_Kd), and normal map from norm/map_Bump. Route d<1 or illum 4/6/7 to Dielectric(Ni).
- **[io-13]** medium · confirmed · `src/io/gltf_loader.cpp:243` — Sparse accessors reject the whole glTF; Draco/meshopt primitives silently become zero-position geometry
  - Düzeltme: Use cgltf_accessor_unpack_floats (which handles sparse) into a temp buffer. Detect extensionsRequired containing draco or meshopt and report a clear error, or decode them. Convert triangle strips and fans.
- **[io-14]** medium · confirmed · `src/scene/cornell_box.h:50` — Mixed units, no unit/up-axis handling, no auto-framing on import
  - Düzeltme: Pick meters (ROADMAP:207/237). Add an import options struct {unitScale, upAxis, center/onGround}. After import, compute the world AABB, as loadSampleScene already does (428-439), and fit the orbit camera (radius = extent / tan(fov/2)). Make light-add defaults, drag speed and ray epsilon relative to scene bounds.
- **[io-16]** medium · confirmed · `src/scene/material_library.cpp:158` — Material library behavior is hard-coded by preset id; JSON cannot express glass, IOR or type
  - Düzeltme: Add "type": "disney|dielectric", "ior", "transmissionColor", and texture-map keys to the JSON, drop the id checks, and put type and ior into the existing presets.
- **[io-17]** medium · confirmed · `src/io/obj_loader.cpp:90` — Drag-dropped non-ASCII paths fail: GLFW gives UTF-8, every loader opens files with narrow ANSI APIs
  - Düzeltme: Set <activeCodePage>UTF-8</activeCodePage> in an app manifest (one file), or convert UTF-8 to std::filesystem::path via std::u8string, open files with it, and give cgltf/tinyobj in-memory buffers. Loop over all dropped paths.
- **[io-m5]** medium · verifier-found · `src/app/application.cpp:1589` — Transform drag resets accumulation every frame but geometry only moves on release
  - Düzeltme: Use per-instance transforms (TLAS/BLAS) so a transform edit re-fits only the top-level BVH, or rebuild throttled while dragging.
- **[io-18]** low · confirmed · `src/scene/scene_graph.cpp:90` — Sphere nodes ignore world scale/rotation; SceneNodeType::Light is never used
  - Düzeltme: Scale the radius by the max column length of the world matrix (or bake spheres to meshes under non-uniform scale). Move lights into the graph as Light nodes compiled by compileNode.

## Uygulama mimarisi ve UX `app`

application.cpp is a god object. One `Application` class with about 52 private methods (application.h:120-171) shares one `AppState` bag of about 50 fields (application.h:50-104). That one class owns the window and ImGui setup, the asset library, sample scenes, import, project I/O, environment and light setup, the preview render thread, the full-render and turntable jobs, every panel, and input handling. The threading is the most serious problem. There is one `imageMutex`, and the preview thread holds it for the whole sample pass (application.cpp:609-618). UI edits change shared objects in place with no lock: the env-map Image, DisneyMaterial textures, scene nodes, the camera and the settings. Structural edits recompile the whole scene on the UI thread, which re-bakes every vertex and rebuilds the BVH. This leads to use-after-free crashes in normal use: switching HDRI while the preview i

- **[app-1]** critical · confirmed · `src/app/application.cpp:818` — Environment map Image is resized/overwritten in place while render threads sample it (use-after-free / out-of-bounds)
  - Düzeltme: Make the env Image immutable and owned by the light: `auto img = std::make_shared<const Image>(std::move(*loaded)); env = std::make_shared<EnvironmentLight>(img, ...)`. Then publish it through the scene-snapshot swap (see app-6). Never write to an Image a render thread can see. This also removes the pixel-by-pixel copy at 818-823.
- **[app-2]** critical · confirmed · `src/scene/scene_graph.cpp:88` — Clearing or deleting scene nodes frees materials still referenced by raw Material* in the preview and final-render scenes
  - Düzeltme: Have each Shape (or the Scene) hold a std::shared_ptr<const Material>, or give the Scene a `std::vector<std::shared_ptr<Material>>` that owns every material it references, so a compiled scene keeps its materials alive. Then build the scene on the UI thread and hand the worker an immutable shared_ptr<const Scene>.
- **[app-3]** critical · confirmed · `src/app/application.cpp:557` — Undo/redo lambdas capture raw SceneNode* and are never invalidated, so Ctrl+Z/Ctrl+Y after a delete or an import undo is a use-after-free
  - Düzeltme: Store a stable node id (pickId, or a uint64 that is never reused) in each command and resolve it with graph.findByPickId at execution time, skipping the step if the node is gone. Make delete an undoable command that holds the removed unique_ptr<SceneNode>, so undo re-inserts the same node.
- **[app-4]** high · confirmed · `src/app/application.cpp:1141` — Full render: output discarded when path is empty (default), no cancel, app close blocks until finished, no ETA, result never shown
  - Düzeltme: Default outputPath to something like `renders/<project>_<timestamp>.png`, or keep the last result in memory and show it in a 'Render output' window with a Save button. Pass `std::atomic<bool>& cancel` into renderProgressive and check it each pass (or each tile). Set it in the destructor and add a 'Durdur' button. Record start time with steady_clock and show elapsed time plus ETA = elapsed/spp*(target-spp). Clamp the inputs to at least 1.
- **[app-5]** high · confirmed · `src/app/application.cpp:1706` — Texture slot edits swap DisneyMaterial textures on the UI thread while render threads dereference them; also synchronous disk decode and no undo
  - Düzeltme: Load the texture first (on a worker), then apply the change as a copy-on-write material: clone the DisneyMaterial, set the texture on the clone, swap node->material, and publish a new scene snapshot. Push an undo step with the before and after material shared_ptrs, the same way pushMaterialUndo does.
- **[app-6]** high · confirmed · `src/app/application.cpp:609` — One mutex is held for an entire sample pass; the UI thread takes it every pass, so UI frame time and camera latency are bounded by pass time
  - Düzeltme: Render into a buffer private to the render thread. Under a short lock, copy only the camera, the settings and a shared_ptr<const Scene> snapshot. Publish the result by swapping the front and back buffers (or copying into a staging Image) under a brief lock. Add an atomic generation counter that tiles check, so a camera change cancels the in-flight pass. Use a condition_variable instead of the 1 ms sleep loop.
- **[app-7]** high · confirmed · `src/app/application.cpp:569` — Every structural edit recompiles the whole scene on the UI thread: all vertices re-baked and the full BVH rebuilt, even for light or environment changes
  - Düzeltme: Split the rebuild by what changed. Lights and environment: rebuild only Scene::m_lights and swap. Material: store a material index or shared slot per mesh so swapping it needs no re-bake. Transform: build one BLAS per mesh in object space and a TLAS over instances, so a transform change refits only the top level. ROADMAP F10 'Canlı malzeme düzenleme ... sahne yeniden derlenmez' (ROADMAP.md:538) already asks for this.
- **[app-8]** high · confirmed · `src/app/application.cpp:363` — First-run and import defaults: app boots into a Cornell box, 'Ornek Sahne' loads an empty scene but reports success, imported models get no ground and no auto-framing, and lights use Cornell units
  - Düzeltme: Ship the three OBJs, or switch the check to sample_box.gltf. On import: compute the world AABB, call placeGroundUnder (already exists), and fit the camera with radius = boundingRadius / sin(fov/2), target = box center. Scale default light size and offset by the scene's bounding radius. Report an error when the sample assets are missing. Draw a letterbox or safe-frame overlay that matches the output aspect.
- **[app-m1]** high · verifier-found · `src/app/application.cpp:1813` — Text typed into the full-render 'Cikti Dosyasi' field is discarded, so only 'Gozat...' can set an output path
  - Düzeltme: Edit a persistent buffer: use InputText on std::string through imgui_stdlib, or copy outPath back to outputPath whenever InputText returns true.
- **[app-9]** medium · confirmed · `src/app/application.cpp:1172` — Silent failures and false success messages; the project file omits lights, the generated ground and render settings
  - Düzeltme: Check every save and load result and call setStatus with a clear error state (red text, shown longer). Add Save As through showFileDialog. Serialize lights, settings and generated primitives. Store paths relative to the project file.
- **[app-10]** medium · confirmed · `src/app/application.cpp:1776` — Tone-map and exposure changes throw away converged samples, and 'Export' saves a different image than the viewport shows
  - Düzeltme: Replace markDirty() with a flag that only re-runs the upload (e.g. `imageReady = true`) for tonemap and exposure. For export, either save exactly what is displayed (denoised when shown) and label it 'Onizlemeyi kaydet (WxH)', or route Export through the full-render path.
- **[app-11]** medium · confirmed · `src/app/application.cpp:1589` — Transform drag restarts accumulation but doesn't move geometry in the path-traced view until mouse release; only translation is editable
  - Düzeltme: Once a TLAS exists (app-7), update the instance transform and refit on every drag tick. Until then, call a throttled rebuild during the drag. Scale the drag speed by scene radius, and add rotation and scale fields (or ImGuizmo, as ROADMAP F10 plans).
- **[app-12]** medium · confirmed · `src/app/application.cpp:612` — Shared scene state is read by render threads while the UI writes it with no synchronization
  - Düzeltme: Put the UI thread in sole ownership of the editable document. Snapshot (camera, settings, shared_ptr<const Scene>) into a value struct under a short lock, and give it to workers. Do the compile for a final render on the UI thread before spawning the worker.
- **[app-13]** medium · confirmed · `src/ui/file_dialog.cpp:18` — File paths: ANSI Win32 dialog with a MAX_PATH buffer, UTF-8 drop paths given to narrow-string APIs, and no dialog at all on non-Windows
  - Düzeltme: Use IFileOpenDialog/IFileSaveDialog with FOS_PICKFOLDERS for the turntable folder. Convert wide strings to UTF-8 with WideCharToMultiByte(CP_UTF8). Add a UTF-8 activeCodePage manifest and /utf-8 so all narrow strings are UTF-8. On Linux, use a small portable dialog library or an ImGui fallback. Route .photon drops to project load and loop over every dropped path.
- **[app-14]** medium · confirmed · `src/app/application.cpp:1711` — A texture dropped with nothing selected stays pending and is silently applied as albedo to the next mesh the user selects
  - Düzeltme: In processPendingDrops, apply the texture to the part under the cursor (pick at the drop position) or to the current selection. Otherwise only register it in the Doku library and call setStatus. Clear pendingTextureDrop in the same frame either way.
- **[app-m2]** medium · verifier-found · `src/app/application.cpp:1560` — The 'Ilk isik yogunlugu' slider changes AreaLight radiance but not the emissive quad baked into the BVH, so the two MIS halves use different radiance
  - Düzeltme: Call rebuildScene() when the slider is released, or have the emissive quad read its radiance from the AreaLight instead of a copy.
- **[app-m3]** medium · verifier-found · `src/engine/scene.cpp:52` — Scene::addLight rebuilds the full BVH once per area light, on every compile
  - Düzeltme: Add the light quads before the single buildAccelerator call in SceneGraph::compile, or make buildAccelerator lazy with a dirty flag.
- **[app-15]** low · confirmed · `src/app/application.cpp:1968` — Click-to-select fires on mouse-down, so every orbit drag that starts on empty background clears the selection
  - Düzeltme: Pick on IsMouseReleased(Left) only when the drag distance is below a threshold (io.MouseDragMaxDistanceSqr[0] < 9), and do not clear the selection when the click lands on background during orbiting.
- **[app-16]** low · plausible · `src/app/application.cpp:222` — Latin-1-only font range forces ASCII-only Turkish, em dashes show as '?', labels mix Turkish and English, and debug output plus a console window are left in
  - Düzeltme: Build glyph ranges with ImFontGlyphRangesBuilder: AddRanges(GetGlyphRangesDefault()), add 0x0100-0x017F and 0x2014, then use proper Turkish strings, kept in one string table. Remove the debug blocks. Set WIN32_EXECUTABLE (with a WinMain or /ENTRY:mainCRTStartup). Dock Render in its own node.
  - Not: The headline claim is wrong for this build. CMake used FetchContent ImGui 1.93 WIP (build/CMakeCache.txt: imgui_DIR NOTFOUND; imgui.h:32), and its OpenGL3 backend sets RendererHasTextures (imgui_impl_opengl3.cpp:1106). Glyphs are therefore loaded on demand and the default-range limit does not apply. The UTF-8 source bytes also survive MSVC's ACP round-trip with no BOM, so em dashes should render. The debug prints are capped at 4, 3 and 2 frames by `static int once`, so they are not per-frame. What remains true is minor: MessageBoxA receives UTF-8 text (main_app.cpp:17), labels mix Turkish and English, <cstdio> is included twice (31, 34), and there is no WIN32 subsystem, so a console window opens.

## Görsel tasarım, tema, GL önizleme `ui`

What the screenshots show. cornell-viewport-siyah.png: a 3-column dock layout (Kutuphane | Viewport | Sahne + Inceleyici/Render) with a status dock at the bottom. The viewport is uniformly dark at "4 fps 95 spp": no image, no overlay, no error message. So yes, it is broken. At HEAD, loadCornellScene set theta=0. That puts the eye on +Y, parallel to the up vector, which collapses the camera basis (perspective_camera.cpp:18 up.cross(w).normalized()) and turns every ray into NaN. The fix is only in the uncommitted diff (application.cpp:484-487), along with an RGB->RGBA unpack-alignment fix (viewport_texture.cpp:35-52) and three leftover debug fprintf blocks. The same screenshot shows several more problems: category headers in the material library are drawn on top of the thumbnails (a SameLine bug); metal thumbnails render as black balls; every thumbnail sits on a saturated steel-blue tile; 

- **[ui-1]** high · confirmed · `src/app/application.cpp:2139` — UI thread blocks on the render thread's pass-long mutex, so viewport and orbit run at the path tracer's pass rate (4 fps in the screenshot)
  - Düzeltme: Do not hold the mutex during the pass. Render into a render-thread-private accumulation buffer, then copy or swap into a shared 'display' Image under a short lock at the end of the pass. Lazy alternative: in the UI use std::unique_lock(m, std::try_to_lock) and, if it fails, set imageReady back to true and skip the upload this frame. Add an atomic cancel flag that renderSamplePass checks per tile so markDirty() aborts the in-flight pass.
- **[ui-3]** high · confirmed · `src/ui/orbit_camera.cpp:41` — Zoom is about 0.2% per wheel notch and inverted; pan does not track the cursor; radius clamp ignores scene scale
  - Düzeltme: Use multiplicative dolly with the conventional sign: `radius *= std::pow(0.85f, delta);`. Pass the viewport height and FOV into pan() and move by 2*r*tan(fov/2)/h per pixel (and orthoH/h in ortho). Derive the radius clamp from the scene AABB diagonal (for example [1e-3*d, 100*d]). Add F = frame selection/all using the AABB walk already in loadSampleScene (application.cpp:428-439).
- **[ui-5]** high · confirmed · `src/app/application.cpp:818` — Loading an HDR rewrites m_state.envMap while the render thread may be sampling it (data race, possible crash)
  - Düzeltme: Load into a new Image, then inside std::lock_guard(imageMutex) swap it into m_state.envMap and build the new EnvironmentLight (or hold the env as shared_ptr<Image> owned by the light). Simplest: take imageMutex at the top of applyHdrEnvironment and applyEnvironmentEntry and make rebuildScene's lock re-entrant-free by splitting it into a locked helper.
- **[ui-m1]** high · verifier-found · `src/app/application.cpp:1025` — Studio presets load zero lights: the 'lights' array is cut at its first inner ']'
  - Düzeltme: Find the matching ']' by bracket-depth counting (or brace-match each {...} object) before slicing; better still, use a real JSON parser for studio files.
- **[ui-4]** medium · confirmed · `src/app/application.cpp:1778` — Changing exposure or tone-map operator restarts path tracing; CPU tone-map and full texture reallocation run on the UI thread every pass
  - Düzeltme: Lazy fix: for tmo/exposure, set a 'reupload' flag instead of calling markDirty. Proper fix: upload linear RGB as GL_RGBA16F with glTexImage2D only on resize and glTexSubImage2D otherwise. Draw it through a small fullscreen shader that applies exp2(EV) and the selected TMO (the ACES code is already in kMeshFS, gl_preview.cpp:236-239). Exposure and TMO then cost nothing and never reset accumulation.
- **[ui-6]** medium · confirmed · `src/preview/gl_preview.cpp:643` — GL preview env cubemap never refreshes when switching environments (cache keyed by Image pointer + size)
  - Düzeltme: Pass an environment revision counter (increment it in applyHdrEnvironment and applyEnvironmentEntry) to GLPreview::render and compare that instead of the pointer.
- **[ui-7]** medium · confirmed · `src/app/application.cpp:1968` — Viewport input has no mouse capture; click-select fires on press, so starting an orbit on empty space deselects; selection has no viewport highlight
  - Düzeltme: Place `ImGui::SetCursorScreenPos(rmin); ImGui::InvisibleButton("##vp", imageSize, ImGuiButtonFlags_MouseButtonLeft|Right|Middle)` over the images and drive orbit and pan from IsItemActive() so the item captures the mouse. Pick on IsMouseReleased(Left) only when the total drag delta is below the drag threshold. Pass the selected pickId to GLPreview and draw an outline (stencil pass or inflated back-face pass) in the accent colour.
- **[ui-8]** medium · confirmed · `src/app/application.cpp:1291` — Material library grid: category headers drawn on top of the previous row's thumbnails
  - Düzeltme: Call SameLine only between items: `if (col % 3 != 0) ImGui::SameLine();` before drawing the item (skipping the first), or lay the grid out with BeginTable using an auto column count = floor(availX / (thumb + spacing)).
- **[ui-9]** medium · confirmed · `src/scene/material_library.cpp:78` — Material thumbnails: metals render black, linear values written without gamma, every tile sits on a saturated accent background
  - Düzeltme: Render thumbnails with the existing GLPreview IBL/GGX path (drawSphere + env cube into an offscreen FBO at 128 px, i.e. 2x) instead of a separate CPU Phong model. Push ImGuiCol_Button transparent and Hovered/Active to bg3 around the grid. Draw the preset name under each tile with TextDisabled.
- **[ui-11]** medium · confirmed · `src/app/application.cpp:222` — Typography: one font at one size, no icon font, no DPI scaling, ASCII-only Turkish because MSVC lacks /utf-8
  - Düzeltme: Add /utf-8 to CMakeLists.txt:42 and restore diacritics in the strings. Ship Inter (OFL) at 15/18 px plus a merged icon font (MergeMode=true) such as Lucide or Material Symbols. Read the content scale from GLFW and apply style.ScaleAllSizes(scale) and style.FontScaleDpi = scale after applyKeyShotTheme().
- **[ui-13]** medium · confirmed · `src/preview/gl_preview.cpp:275` — Raster preview shading is far from the path tracer (no shadows, crude IBL, textures/glass/emission ignored, ACES hard-coded, half-res without AA)
  - Düzeltme: Step by step, cheapest first: pass settings.tmo to the shader; render at full framebuffer resolution with a 4x MSAA renderbuffer (trivial on a GTX 1080); prefilter the cube once per environment (GGX importance-sampled mips plus SH9 irradiance) and add a 2D BRDF LUT (split-sum); one shadow map for the key light; bind the albedo map. Remove PreviewQuality or wire it to the menu.
- **[ui-2]** low · plausible · `src/ui/orbit_camera.cpp:7` — Orbit camera can reach theta = 0 or pi, the root cause of the black Cornell viewport; still reachable via project and studio JSON
  - Düzeltme: Clamp once where every caller goes through: in getPosition() (or a single sanitize() called by makeCamera and the preview) clamp theta to [1e-3, pi-1e-3] and radius to > 0. Rename the studio keys to elevation/azimuth, or map yaw->phi and pitch->(pi/2 - theta). Optionally count NaN samples in the accumulator and show a viewport warning.
  - Not: Two of the claims are wrong. First, nothing produces NaN: Vec3f::normalized() returns a zero vector when the length is 0 (vec.h:147-150), so at theta=0 all rays become -w and the image is one flat colour. Second, theta=0 did not cause the black screenshot: viewport pixels sample exactly (20,20,21) = bg0 / glClearColor (theme.cpp:34, application.cpp:2181), so the CPU image was not drawn at all. That screenshot (16:55) predates the uncommitted RGB-alignment heap-corruption fix in viewport_texture.cpp (17:56). The surviving part is real but minor: theta is unclamped in applyCameraJson (730) and applyStudioPreset (998). The yaw→theta naming is semantically swapped, but the shipped data is consistent: product_studio.json has yaw 1.15 / pitch 0.42, matching frameProductCamera theta 1.15 / phi 0.42 (374-375).
- **[ui-10]** low · confirmed · `src/app/application.cpp:2051` — Status bar is a tabbed dock window ~4.5% tall with hard-coded x offsets; text clipped; no elapsed time or ETA
  - Düzeltme: Use ImGui::BeginViewportSideBar("##status", vp, ImGuiDir_Down, ImGui::GetFrameHeight(), flags), available in the installed imgui_internal.h:3765 (1.92.9b), instead of a docked window, and lay it out with SameLine() spacing or a 1-row table. Record a std::chrono start time in markDirty and show elapsed time and ETA = elapsed*(target-spp)/spp.
- **[ui-12]** low · confirmed · `src/ui/theme.cpp:67` — Theme gaps: default ImGui blue leaks in, window-menu triangles on every panel, every button is accent-coloured (Render override is a no-op)
  - Düzeltme: Make Button bg3, Hovered 0.22 grey, Active accentLo; keep the accent for one primary Render button with a stronger colour. Set the overline and new colours to the accent or bg colours. Set s.WindowMenuButtonPosition = ImGuiDir_None and s.TabBarOverlineSize. Make Header neutral (bg2/bg3) with an accent only for the selected tree row.
- **[ui-14]** low · plausible · `src/app/application.cpp:594` — Path-traced viewport is capped at 960 px on the long side and stretched, so even the converged viewport is never pixel-sharp; logical pixels are used, not framebuffer pixels
  - Düzeltme: Use the cap only for the first passes after markDirty (a resolution ladder of 1/4 -> 1/2 -> 1), then render at native framebuffer pixels (w*DisplayFramebufferScale.x). Keep the preview size separate from settings.width/height, which should belong to the final render.
  - Not: The 960 cap and the overwrite of settings.width/height are real (application.cpp:594-598), and the turntable export inherits that size (297-298). It is overstated, though: the default layout leaves about a 1020 px central viewport (1680 minus 20% left, minus 24% right; see the screenshot), so the scale is about 0.94, not 1.7x. The HiDPI claim does not hold on Windows, because GLFW window coordinates are physical pixels there (framebuffer scale 1).
- **[ui-15]** low · confirmed · `src/app/application.cpp:1525` — Panel information architecture: Camera always tops the Inspector, Render panel hidden behind an Inspector tab, 7 library tabs overflow
  - Düzeltme: Inspector shows the selection (Object -> Material -> Textures). Camera and Environment move into the Render/Scene panel or a viewport toolbar popover. Use an icon-only vertical tab strip for the library (or merge Studio/Isik/Ortam into 'Ortam').
- **[ui-16]** low · confirmed · `src/preview/viewport_texture.cpp:60` — Dead immediate-mode GL draw path and leftover debug fprintf in the viewport and frame loop
  - Düzeltme: Delete ViewportTexture::draw and resize() (resize only stores sizes). Remove the three debug blocks and the duplicate include before committing the black-viewport fix.
- **[ui-m2]** low · verifier-found · `src/app/application.cpp:616` — markDirty's currentSpp=0 is overwritten by the in-flight pass, so a stale-camera frame flashes when an orbit starts
  - Düzeltme: Use a generation counter: markDirty increments it, the render thread captures it before the pass and discards or doesn't publish the result if it changed; or set currentSpp only if renderDirty is still false.

## Build, testler, belgeler `build`

The build works on the owner's machine: build-vcpkg/Testing/Temporary/LastTest.log from Oct 01 17:41 shows 62/62 tests passing in 89 ms with MSVC 14.51 and Ninja. But the committed tree is fragile, and most of the docs describe a different engine from the one in the code.

- **[build-1]** medium · confirmed · `vcpkg.json:5` — Committed HEAD is not buildable with vcpkg, and all fixes live only in uncommitted or untracked files
  - Düzeltme: Commit now as ROADMAP.md:216 already plans (`git add -A` covers the untracked files). Add photon_ui.ini.bak and Testing/ to .gitignore first.
- **[build-2]** medium · confirmed · `src/integrators/path_tracer.cpp:113` — README says "unbiased Monte Carlo", but the default sample scene multiplies path throughput by a non-physical AO term
  - Düzeltme: Default aoStrength to 0 everywhere and label it as a stylization option. Add a furnace test that runs with AO off. Fix the README wording.
- **[build-4]** medium · confirmed · `docs/architecture.md:57` — README and architecture.md advertise subsystems that do not exist
  - Düzeltme: Rewrite architecture.md from the CMake graph (see build-7). Remove Halton, Sobol, DirectLighting, AO, Metal and gpu. List the 13 real targets.
- **[build-9]** medium · confirmed · `CMakeLists.txt:42` — Global fast-math with no NaN rejection: one NaN sample poisons a pixel, and future isnan guards are dead under GCC/Clang
  - Düzeltme: Reject non-finite samples in addSample with a bit-pattern check (std::bit_cast exponent test), or compile image.cpp with /fp:precise and -fno-finite-math-only. Make the fast-math flags per-target on photon_* only.
- **[build-10]** medium · confirmed · `tests/CMakeLists.txt:32` — Test suite has no estimator-level checks: no furnace, chi-square, golden image, determinism, Renderer or UI tests
  - Düzeltme: Add, in order: Lambert, Disney and glass furnace tests; a chi-square sample/pdf check per BSDF; a 64×64 golden image with a relMSE threshold; a 1-vs-N-thread bit-exact test; then gtest_discover_tests. Delete Testing/ from git.
- **[build-15]** medium · confirmed · `docs/ROADMAP.md:171` — ROADMAP scope is unrealistic for one developer whose goal is a great renderer plus learning CG; about 60-70 items are commercial-only
  - Düzeltme: Re-cut into learning-ordered milestones. M-a: furnace and chi-square tests, golden images, Owen-Sobol, spherical-rect sampling, Kulla-Conty, AgX/PBR Neutral, sRGB textures. M-b: OIDN prebuilt plus live denoise, preview tiers, center-out tiles, Embree, a persistent pool. M-c: gizmo, drop-to-floor, HDRI browser, live material edit, path-traced thumbnails. Move all commercial and Linux items to an 'if productizing' appendix.
- **[build-m1]** medium · verifier-found · `src/app/application.cpp:366` — Sample product scene is always empty: its OBJ models are missing and *.obj is gitignored
  - Düzeltme: Add an `!assets/models/*.obj` negation to .gitignore and commit the procedural OBJs, or switch loadSampleScene to the committed sample_box.gltf.
- **[build-3]** low · confirmed · `README.md:40` — Fresh clone cannot build with one command; README shows a different, toolchain-less build that picks other dependency versions
  - Düzeltme: README: `. .\scripts\devshell.ps1; cmake --preset release; cmake --build --preset release; ctest --preset release`. Add testPresets. Make the FetchContent fallback opt-in (ROADMAP.md:249 single-source rule).
- **[build-5]** low · confirmed · `docs/file-reference.md:370` — file-reference.md is stale: wrong "stub" labels and missing files and tests
  - Düzeltme: Regenerate the tables from `git ls-files src tests docs assets`. Drop the "Bilinen sınırlamalar" (known limitations) rows that are no longer true.
- **[build-6]** low · confirmed · `CMakeLists.txt:127` — PHOTON_BUILD_GPU=ON fails at configure time: src/gpu does not exist
  - Düzeltme: Delete the option and the docs rows until F4 starts (YAGNI), or guard with `if(EXISTS ...)` plus FATAL_ERROR "not implemented".
- **[build-7]** low · confirmed · `src/integrators/CMakeLists.txt:5` — Module graph is inverted, has a hidden cycle, and module boundaries are not enforced
  - Düzeltme: Move Scene (intersect, lights, environment) below integrators: a photon_render_scene lib that integrators link. Then flip scene→engine as ROADMAP.md:270 plans. Per-module include roots would enforce boundaries.
- **[build-8]** low · confirmed · `CMakeLists.txt:28` — OIDN: the OFF switch cannot disable it, no DLL deploy, no THIRD_PARTY row, wrong install hint in three places
  - Düzeltme: Use a 3-state option (AUTO/ON/OFF), copy `$<TARGET_RUNTIME_DLLS:photon_app>` POST_BUILD, add an Apache-2.0 OIDN row, and replace the hint text with the third_party/oidn prebuilt path.
- **[build-11]** low · confirmed · `CMakeLists.txt:42` — Warnings: the two known warnings are fixed in the working tree but /WX is still off, and /W4 applies to third-party code
  - Düzeltme: Create an INTERFACE target photon_warnings with /W4 /WX and link it from each photon_* lib. Drop the global add_compile_options.
- **[build-12]** low · confirmed · `CMakeLists.txt:53` — AVX2 forced on globally although no code uses intrinsics
  - Düzeltme: Fine for the learning machine. For distribution, use an x86-64-v2 baseline plus runtime dispatch (or Embree, which dispatches internally).
- **[build-13]** low · confirmed · `CMakeLists.txt:86` — FetchContent builds unused third-party targets; THIRD_PARTY.md gets miniz wrong
  - Düzeltme: Set TINYEXR_BUILD_SAMPLE OFF (and similar for tinyobjloader), or use FetchContent with SOURCE_SUBDIR pointing to a dir with no CMakeLists. Fix the miniz row: MIT, unused, stb zlib used instead.
- **[build-14]** low · confirmed · `.gitignore:38` — .gitignore will hide planned OBJ fixtures and doesn't cover Testing/ or photon_ui.ini
  - Düzeltme: Scope `*.obj` to build dirs (or add `!tests/**/*.obj`, `!bench/**/*.obj`). Add Testing/ and photon_ui.ini*.
- **[build-16]** low · confirmed · `docs/ROADMAP.md:9` — ROADMAP "Şu anki durum" (current status) and checkboxes are stale
  - Düzeltme: Update the status block and tick the done boxes. Add the black-viewport bug as a T item.

## Modüller arası sorunlar (mimar ajanı)

- **critical** — The render Scene borrows every mutable object from the editor (materials, env image, textures, lights) (`src/engine/scene.h`, `src/scene/scene_graph.cpp`, `src/lights/environment_light.h`, `src/materials/disney.cpp`, `src/io/gltf_loader.cpp`, `src/app/application.cpp`)
- **high** — Export brightness bug is masked by the fallback denoiser, and every export path behaves differently (`src/core/image/image_io.cpp`, `src/engine/denoiser.cpp`, `src/engine/renderer.cpp`, `src/app/application.cpp`)
- **medium** — One RenderSettings object drives preview, full render and turntable, and is written from three threads (`src/app/application.cpp`, `src/engine/render_settings.h`)
- **high** — No scene scale or unit anywhere; every module hard-codes a different scale (`src/integrators/path_tracer.cpp`, `src/geometry/triangle.cpp`, `src/scene/scene_graph.cpp`, `src/ui/orbit_camera.cpp`, `src/app/application.cpp`, `src/scene/cornell_box.h`)
- **high** — No stable node identity: undo, import-undo, project files, picking and selection each use a different handle (`src/app/application.cpp`, `src/scene/scene_graph.cpp`, `src/scene/scene_node.h`, `src/scene/project_io.cpp`)
- **high** — No shading-frame contract between materials and the integrator (`src/integrators/path_tracer.cpp`, `src/materials/dielectric.cpp`, `src/materials/disney.cpp`, `src/materials/material.h`)
- **medium** — Two geometry truths: the raster layer and picking read SceneGraph, the path tracer reads the baked Scene (`src/app/application.cpp`, `src/preview/gl_preview.cpp`)
- **medium** — Default light rig, uniform light selection and the stratified sampler produce a light-by-light progressive preview (`src/app/application.cpp`, `src/integrators/path_tracer.cpp`, `src/samplers/stratified_sampler.cpp`, `src/engine/renderer.cpp`)
- **medium** — Engine-level scene logic and JSON parsing live in the app, so no headless path can reproduce app renders (`src/app/application.cpp`, `src/scene/project_io.cpp`, `src/main.cpp`)
- **medium** — Image conflates the accumulator, texture and environment roles (`src/core/image/image.h`, `src/core/image/image_io.cpp`, `src/materials/disney.cpp`, `src/lights/environment_light.cpp`)
- **low** — Module graph is cyclic and boundaries are not enforced (`src/integrators/CMakeLists.txt`, `src/integrators/path_tracer.cpp`, `src/engine/CMakeLists.txt`, `src/scene/CMakeLists.txt`, `src/core/CMakeLists.txt`)

## Taramanın kapsamadığı / ek riskler

- Nothing was measured. No auditor ran a profiler, a timer or a memory snapshot, and no rays/s figure exists because main.cpp is a fixed Cornell demo. Every 'Nx faster' in the digest is an estimate. The first deliverable should be a CLI that prints stats.json, so the priorities can be re-ranked on real numbers.
- The preview render thread has no try/catch (application.cpp:583-622). A tile exception propagates through future.get (parallel.h:86-88). So does ThreadPool::submit throwing on a stopped pool (thread_pool.h:53-55) or a bad_alloc in accumImage.resize (599). Any of these calls std::terminate and kills the app silently. The full-render and turntable threads do catch (1153, 355). This was not in the digest.
- Import undo identifies nodes by name (application.cpp:659-665). After two imports of the same file, Ctrl+Z removes the first copy, and redo (670) creates a third node. The digest only flagged the raw-pointer undo captures.
- The m_state.lights vector is mutated without the lock: push_back and clear at 422-425, 868, 874, 1028 and 1417. Meanwhile the full-render and turntable threads iterate it under imageMutex (1125, 328). The UI never takes that mutex for these writes, so it is a data race on the vector itself, not just on light fields.
- The background compile in startFullRender (1123-1124) and the turntable (326-327) reads the SceneGraph while the UI writes it without the lock: visibility checkbox (1501), transform drag (1588), preset assignment (543), addChild (703). The full render can therefore bake a half-updated graph.
- During a transform drag, the GL raster layer, drawn from the graph each frame (1901-1903) at alpha≈1, shows the new position. The restarted CPU passes show the old baked position until mouse release (1607). Both are blended in the same viewport image.
- The turntable export inherits the preview's AO of 0.35 and its maxBounces through an unlocked settings copy (317). It forces denoise off (322), so each frame PNG is a summed buffer, 8x too bright at the default 8 spp. The digest listed the turntable among the export paths but not why its brightness differs from Full Render.
- During a full render, all geometry exists three times: SceneGraph source meshes, flatScene baked copies, and the full render's private baked Scene (1120-1124). There are also two BVHs. The digest counted two copies (geometry-11).
- CWD-relative writes: the ImGui layout file is photon_ui.ini (application.cpp:210, which .gitignore:52 does not cover because it ignores imgui.ini), the project is saved to scene.photon (application.h:90), exports go to export.png (91) and turntable frames to turntable_frames (286). findAssetsRoot walks up to 5 parents (50-58), and the onboarding marker is written into whichever assets folder it finds (811). Launched from the repo root, Ctrl+S writes untracked files into the source tree.
- Claims I did not re-verify myself, which rely on files outside this glue scope: lighting-14 (equirect mirrored versus Mitsuba, still plausible), ui-2 (theta reaching 0 through studio JSON 'yaw'/'pitch'; note applyStudioPreset maps yaw->theta and pitch->phi at 998-999, which looks swapped), engine-12 (36 mm treated as sensor height), core-12 (seed quality), geometry-1 (the 1e-6 determinant epsilon's effect at meter scale), and the materials-2/3 rough-dielectric formulas. I did verify refractVec mirroring, the export sum and flip, the studio parsing, the stratified strata, the glTF non-owning default material, and every lock and race path cited above.
- The AddressSanitizer preset exists (CMakeLists.txt:204-227, CMakePresets asan), but nothing shows the app has ever run under it. A scripted sequence would likely expose app-1/2/3 and io-1 within seconds: load the sample scene, drop an HDR while rendering, delete a node, press Ctrl+Z, import a material-less glTF. No test links Application or exercises threads at all. tests/CMakeLists.txt links only photon_engine and photon_scene.
- The GL raster preview and the material thumbnails were judged only for style and shading. Nobody checked GL resource lifetimes against SceneGraph revisions beyond the VAO cache, or what happens when buildMaterialThumbnails/buildEnvThumbnails run again (844) while ImGui holds the old texture IDs within the same frame.

## UI → render veri akışı (referans)

THREADS:
- UI/main thread: the loop at application.cpp:2123-2185. It owns GL, ImGui, SceneGraph and all AppState writes.
- Preview control thread: startRenderThread, 582-623.
- Global ThreadPool: hardware_concurrency workers (parallel.h:14-17, thread_pool.h:18). Preview, full render, turntable and AOV fill all share it.
- One full-render or turntable thread, sharing fullRender.thread (1118, 315).
- A brand-new ThreadPool on every pass whenever numThreads > 0 (renderer.cpp:20-24, 247-248). renderProgressive calls renderSamplePass once per spp (renderer.cpp:218-219), so a 256-spp full render with a thread count typed into the dialog (1808-1810) spawns and joins 256 pools.
Total: N workers + 3 threads. No thread priority is set, so the UI competes with N rendering workers.

LOCKING: there is exactly one std::mutex, imageMutex (application.h:73). It covers accumImage, flatScene, and the settings.width/height reset (597-598). It does NOT cover:
- orbitCam (written at 1956-1966, read under the lock at 612)
- other RenderSettings fields (sliders at 1566-1570 and 1761-1786, copied at 610)
- SceneGraph (written at 382, 461, 543, 703, 757, 1501, 1516, 1588)
- the m_state.lights vector (422-425, 868, 874, 1028, 1417; iterated by background threads at 328 and 1125)
- DisneyMaterial fields and textures (1656-1715)
- AreaLight radiance (1560)
- m_state.envMap (818-823, 859)

HOW EACH UI EDIT REACHES THE RENDERER:
(1) Camera orbit, pan or zoom (1956-1977). orbitCam is written without the lock, then markDirty runs (577-580): renderDirty=true and currentSpp=0, both atomics, no lock. The in-flight pass cannot be cancelled. When it finishes it overwrites currentSpp with spp+1 (616) and publishes a stale frame (617). Only then does the next loop iteration reset the buffer (589-603) and start a new pass. Latency is the rest of the current pass plus one full new pass.
(2) Material sliders (1656-1692). They write the live DisneyMaterial in place without the lock, then call markDirty. There is no rebuild, so this is cheap, but only because it is a data race against workers reading it in resolve().
(3) Texture slots (1706-1715). setAlbedoMap decodes from disk on the UI thread, then assigns a shared_ptr<Image> that workers are reading (disney.cpp:230-232). That is a non-atomic shared_ptr store, a possible use-after-free.
(4) Area-light intensity (1559-1561). It mutates the shared AreaLight in place. The BVH quad keeps the radiance copied at compile time (scene.cpp:36), so NEE and BSDF hits disagree until the next rebuild.
(5) Exposure and tone-map operator (1774-1778). markDirty throws away the accumulation even though the change is display-only.
(6) Structural edits: presets, visibility, delete, import, env, lights, studio, undo, project, sample scenes. There are 23 rebuildScene call sites. The graph, light list or envMap is changed BEFORE rebuildScene takes the lock: removeNode at 1516, graph.clear at 382/461/757, envMap.resize at 818. Meanwhile the in-flight pass is still dereferencing the old raw Material* and Image*. rebuildScene then waits for that pass, recompiles everything and rebuilds the BVH 1 + nArea times (scene.cpp:52).
(7) Viewport resize (1859-1863). This calls markDirty. The render thread then rewrites settings.width/height (597-598), which the UI reads without the lock (1760, 2056) and startFullRender copies without the lock (1112).
(8) Display (2138-2147). The UI takes the lock once per pass to tone-map on the CPU and call glTexImage2D. When converged with denoise on, it also runs denoiseCopy (soft blur, no AOVs) on the UI thread while holding the lock.
(9) Full render (1098-1159). The background thread compiles a private Scene from the live graph under the lock (1123-1129), while UI writes that skip the lock (1501, 1588, 703) can still race the compile. It then renders for minutes with NO lock while the UI stays editable. Its Scene holds raw Material* and &envMap, so loading an HDR or deleting a node during a full render is a use-after-free. Every pass it copies the whole image (1134). It cannot be cancelled, and ~Application joins it (175), so closing the window blocks until it finishes. The preview thread yields while fullRender.active is set (585-588). However, the window between that check (585) and the lock (609) lets one preview pass overlap the start of the full render on the same global pool.
(10) Turntable (315-360). It copies m_state.settings without the lock from the background thread (317), forces denoise off (322) and checks shutdown only between frames (334).
(11) Picking (1973-1974, 1987-1988). It runs on the UI thread as a brute-force walk over the graph (gl_preview.cpp:896-904). flatScene and its BVH are unused, `(void)flatScene`.

Exceptions: the preview thread lambda (583-622) has no try/catch. parallelFor2D rethrows tile exceptions through future.get (parallel.h:86-88), so any exception, for example bad_alloc or ThreadPool::submit on a stopped pool (thread_pool.h:53-55), reaches std::terminate. The full-render and turntable threads do catch (1153, 355).

REBUILD COST PER STRUCTURAL CLICK (UI thread):
- Wait for the rest of the current pass.
- Copy and transform all vertices (scene_graph.cpp:48-63).
- One SAH BVH build over all triangles (scene_graph.cpp:103), plus one more per area light (scene.cpp:52).
- One EnvironmentLight CDF build when the env changes (environment_light.cpp:43-90, triggered at 824/860).
Multiplied across operations:
- Project load: one compile per import (768 -> 704) plus a final one (805).
- loadSampleScene: about 6 compiles (imports at 389-395, studio env 846/862, studio 1064, final 456).
- A full render: a third copy of all geometry and a second BVH next to the source graph and flatScene (1120-1124).

UNSAFE-OR-RACY PATHS THAT CAN CRASH IN NORMAL USE:
- HDR or env switch while rendering (818, 859).
- Delete a node, scene/sample/project load, or preset apply while a pass runs (1516, 382, 461, 757, 543, then materials freed under raw pointers).
- Texture slot change (1706-1715).
- Ctrl+Z after a delete (raw SceneNode* captured at 557-566 and 1596-1606).
- Importing a material-less glTF (non-owning shared_ptr at gltf_loader.cpp:231-232 to the local defaultMat at application.cpp:675).
