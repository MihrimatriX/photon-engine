/// @file vec.cpp
/// @brief Compilation unit for vec.h.
///
/// All Vec2f/Vec3f/Vec4f methods are inline or constexpr and live entirely
/// in the header. This .cpp exists as a build-system anchor so the
/// translation unit is compiled and any ODR-use of static constexpr
/// members is satisfied.

#include "core/math/vec.h"

// Intentionally empty — all implementations are header-only.
