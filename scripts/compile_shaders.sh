#!/usr/bin/env bash
# compile_shaders.sh — GLSL 4.60 yol izleyici çekirdeklerini gömülü C++ SPIR-V başlıklarına derler.
# Üretilen başlıklar git'te izlenir; böylece Windows CI dahil hiçbir makinede Vulkan SDK / glslang gerekmez.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SHADERS="$ROOT/src/engine/shaders"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

if ! command -v glslangValidator >/dev/null 2>&1; then
    echo "Hata: glslangValidator bulunamadı (glslang-tools paketini kurun)." >&2
    exit 1
fi

spv_to_header() {
    local spv="$1"
    local var="$2"
    local out="$3"
    local mode="$4" # write | append
    python3 - "$spv" "$var" "$out" "$mode" <<'PY'
import struct, sys
spv_path, var_name, out_path, mode = sys.argv[1:5]
data = open(spv_path, "rb").read()
assert len(data) % 4 == 0, f"Invalid SPIR-V size: {len(data)}"
words = struct.unpack(f"<{len(data)//4}I", data)
f = open(out_path, "w" if mode == "write" else "a", encoding="utf-8")
if mode == "write":
    f.write("// Otomatik üretildi (scripts/compile_shaders.sh) — elle düzenlemeyin.\n")
    f.write("#pragma once\n#include <cstdint>\n#include <cstddef>\n\nnamespace photon::shaders {\n\n")
f.write(f"inline constexpr uint32_t {var_name}[] = {{\n")
for i in range(0, len(words), 8):
    chunk = ", ".join(f"0x{w:08x}u" for w in words[i:i+8])
    f.write(f"    {chunk},\n")
f.write("};\n")
f.write(f"inline constexpr size_t {var_name}Size = sizeof({var_name});\n\n")
f.close()
PY
}

close_header() {
    echo "} // namespace photon::shaders" >> "$1"
}

echo "[1/6] pathtracer.comp -> pathtracer_spv.h (Vulkan 1.1 Compute SAH BVH2)..."
glslangValidator -V --target-env vulkan1.1 -I"$SHADERS" \
    "$SHADERS/pathtracer.comp" -o "$TMPDIR/pt.spv"
spv_to_header "$TMPDIR/pt.spv" "kPathTracerComputeSpv" "$SHADERS/pathtracer_spv.h" write
close_header "$SHADERS/pathtracer_spv.h"

echo "[2/6] pathtracer.comp (-DPHOTON_USE_RAY_QUERY) -> pathtracer_rt_spv.h (Vulkan 1.2 Hardware RT)..."
glslangValidator -V --target-env vulkan1.2 -DPHOTON_USE_RAY_QUERY -I"$SHADERS" \
    "$SHADERS/pathtracer.comp" -o "$TMPDIR/pt_rt.spv"
spv_to_header "$TMPDIR/pt_rt.spv" "kPathTracerRayQuerySpv" "$SHADERS/pathtracer_rt_spv.h" write
close_header "$SHADERS/pathtracer_rt_spv.h"

WF_OUT="$SHADERS/pathtracer_wf_spv.h"
STAGES=("Gen" "Intersect" "Shade" "Shadow")
for s in 0 1 2 3; do
    echo "[$((s+3))/6] wf_pathtracer.comp (WF_STAGE=$s: ${STAGES[$s]})..."
    glslangValidator -V --target-env vulkan1.1 -DWF_STAGE=$s -I"$SHADERS" \
        "$SHADERS/wf_pathtracer.comp" -o "$TMPDIR/wf_${s}.spv"
    mode="append"
    [[ "$s" == "0" ]] && mode="write"
    spv_to_header "$TMPDIR/wf_${s}.spv" "kWf${STAGES[$s]}Spv" "$WF_OUT" "$mode"
done
# Donanım RT kesişim aşaması (WF_STAGE=1 + PHOTON_USE_RAY_QUERY)
echo "[+] wf_pathtracer.comp (WF_STAGE=1 + PHOTON_USE_RAY_QUERY)..."
glslangValidator -V --target-env vulkan1.2 -DWF_STAGE=1 -DPHOTON_USE_RAY_QUERY -I"$SHADERS" \
    "$SHADERS/wf_pathtracer.comp" -o "$TMPDIR/wf_1_rt.spv"
spv_to_header "$TMPDIR/wf_1_rt.spv" "kWfIntersectRtSpv" "$WF_OUT" append
close_header "$WF_OUT"

echo "Tamamlandı:"
ls -lh "$SHADERS/pathtracer_spv.h" "$SHADERS/pathtracer_rt_spv.h" "$SHADERS/pathtracer_wf_spv.h"
