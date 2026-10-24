# Ölçüm tabanı

Makine: i7-6700K (4 çekirdek / 8 iş parçacığı), 32 GB, Windows 11, MSVC 19.51 Release (/O2, /arch:AVX2, /fp:fast).
Sahne: `bench/scenes/showcase.photon` (ürün vitrini: ~13 bin üçgen, 3 alan ışığı + HDRI, cam, krom, araç boyası).
Komut: `photon_render bench/scenes/showcase.photon --res 960x540 --spp 32 --no-denoise`.
"Örnek" = bir kamera yolu (8 sekmeye kadar). Birim: milyon örnek / saniye.

| Tarih | Değişiklik | 8 thread | 1 thread |
|---|---|---|---|
| 2026-10-06 | Başlangıç (kendi BVH, her adayda tam gölgelendirme) | 2.32 | 0.46 |
| 2026-10-06 | Yalın üçgen testli BVH gezinmesi | 2.12 (ölçüm gürültüsü; darboğaz değilmiş) | — |
| 2026-10-06 | Embree 4 + `isDelta` / `evalPdf` (köşe başına 3 gereksiz BSDF değerlendirmesi kalktı) | **3.64** | **0.72** |

Tek thread profil (Embree öncesi, köşe başına çevrim payı): kesişim %44, malzeme değerlendirme %27,
ışık örnekleme %6, ortam değerlendirme %3, kalan (örnekleyici, Rus ruleti, ofset) %20.

1080p, 128 örnek, OIDN: 78 sn (`showcase_1080p.png`, `showcase_stats.json`).

## Linux bulut kabı (2026-10-24, sürüm 0.2.0)

Makine: 4 vCPU (AVX2), GCC 13.3 `-O3 -mavx2 -mfma -ffast-math`, Embree ve OIDN yok (motorun kendi
BVH'si). Mutlak hızlar yukarıdaki i7-6700K satırlarıyla karşılaştırılamaz; burada önemli olan aynı
makinedeki eski/yeni farkıdır.

**Gürültü (eşit örnek).** `showcase.photon`, 480×270, `--no-denoise`; referans: yeni motorla 2048 spp.
relMSE = ort((x − ref)² / (ref² + 0.01)) — `scripts/compare_exr.py`.

| | 16 spp relMSE | 64 spp relMSE | ortalama parlaklık |
|---|---|---|---|
| önceki commit (`36e61ef`: düzgün ışık seçimi, alan örneklemesi) | 0.1428 | 0.0422 | 0.4354 |
| yeni (noktaya özel ışık seçimi + küresel dikdörtgen) | **0.0743** | **0.0200** | 0.4354 |

Örnek başına süre ±%10 içinde aynı; eşit sürede verim ~1.9×. Ortalamalar referansla aynı (yansız).

**Uyarlamalı örnekleme.** Aynı sahne, `--noise 0.05 --spp 512`: piksel başına ortalama 347 örnek,
relMSE 0.0024. Düzgün örneklemenin 347 spp'deki beklenen değeri (160 spp ölçümünden 1/N ile)
0.0036 → aynı örnek bütçesinde ~1.5× daha az hata.

**Yeni sahneler** (`bench/scenes/`, `scripts/gen_material_scenes.py`): `materials_volume` (SSS, renkli
ve bulanık cam, dispersiyon), `materials_metal` (ölçülmüş metaller, ince film, pullar), `sky_shadow_catcher`,
`spot_ies`, `bokeh_imager`, `features_misc` (yuvarlatılmış kenar, sabun köpüğü, ince pencere).
1200×500, 320 spp, `--clamp 20` render'ları `docs/img/v2-*.jpg`.
