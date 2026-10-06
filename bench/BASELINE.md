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
