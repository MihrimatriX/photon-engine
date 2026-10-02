# PhotonEngine yol haritası

Bu belge yalnız özet. Tek doğruluk kaynağı:

- **Plan ve gerekçeler:** [`plan.md`](../plan.md)
- **Görev listesi (kutucuklar):** [`memory-bank/01-gorevler.md`](../memory-bank/01-gorevler.md)
- **Bulgular (dosya:satır, düzeltme önerisi):** [`memory-bank/02-bulgular.md`](../memory-bank/02-bulgular.md)
- **Bağlam ve kaldığımız yer:** [`memory-bank/00-baglam.md`](../memory-bank/00-baglam.md)

Önceki, KeyShot'u geçmeyi hedefleyen 297 maddelik yol haritası `299f357` commit'inde duruyor. Ticari maddeleri aşağıdaki "Ürünleşme" ekine taşındı.

## Hedef

1. **Ürün:** kullanımı kolay, son derece hızlı ve fiziksel olarak gerçekçi bir render motoru, şık bir arayüzle.
2. **Öğrenme:** bu proje üzerinden bilgisayar grafiği ve CGI pipeline'ının tamamı.

Sıra: önce doğruluk ve kararlılık, sonra ölçüm, sonra hız.

## Fazlar

| Faz | Konu | Efor | Kanıt |
|---|---|---|---|
| 0 | Zemin: derleme, belgeler, örnek varlıklar | 2–3 gün | `ctest` yeşil, belgeler kodla uyumlu |
| 1 | Çökme yok, görüntü doğru: sahiplik, Film, sRGB, cam | 2–3 hafta | `test_export_roundtrip`, `test_snell`, `test_texture_colorspace`, ASan senaryosu |
| 2 | Ölçüm: CLI + `stats.json`, fırın/chi-kare/golden testler, bench | 1–2 hafta | `bench/BASELINE.md` ilk satırı |
| 3 | Anında tepki: UI/render ayrımı, iptal, GPU ton eşleme, OIDN | 2–3 hafta | orbit ≥ 60 fps, ilk kare < 50 ms |
| 4 | Kolay ve şık: ilk açılış, gizmo, tipografi, tema | 3 hafta | görev süresi < 2 dk |
| 5 | Örnekleme ve ışık taşıma: Sobol, BSDFSample, alias tablosu | 3 hafta | eşit sürede varyans düşüşü |
| 6 | Hızlandırma: yalın BVH, BLAS/TLAS, Embree 4 | 3 hafta | Mrays/s artışı |
| 7 | Malzeme ve renk: VNDF, Kulla–Conty, OpenPBR, glTF uzantıları | 4–5 hafta | fırın ≈ 1, Mitsuba relMSE < %1 |
| 8 | Rasterizasyon izi: split-sum IBL, gölge haritası | 2–3 hafta | raster/path-trace fark haritası |
| 9 | GPU: GTX 1080'de CUDA 12.x | 6–10 hafta | CPU–GPU eşleşme testi |
| 10 | İleri konular (seçmeli) | — | — |

## Şu anki durum

Güncel durum `memory-bank/00-baglam.md` → "Kaldığımız yer" bölümünde tutulur; burada tekrarlanmaz.

## Ürünleşme eki (karar bekliyor)

EULA ve gizlilik metni, lisans anahtarı, kod imzalama, kurulum paketi, Linux CI ve paketleme, ağ render, STEP/IGES/USD/FBX içe aktarma, Python ve eklenti API'si, KeyShot/Cycles karşılaştırması, patent incelemesi, telemetri.

Şimdiden geçerli tek kural: GPL bağımlılık yok; LGPL yalnız dinamik bağlanır; her bağımlılık [`THIRD_PARTY.md`](THIRD_PARTY.md)'ye yazılır.
