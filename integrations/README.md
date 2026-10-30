# 3ds Max, Maya ve Blender ile PhotonEngine

PhotonEngine'e DCC sahnesi getirmenin üç yolu var:

| Yol | Ne zaman | Kurulum |
|---|---|---|
| **Canlı bağlantı** (önerilen) | Modelleme sürerken: tek tıkla gönder, değiştirince yeniden gönder | DCC'ye bu klasördeki betiği bir kez kur |
| **Dosyayı doğrudan aç** (`.max`, `.ma`, `.mb`, `.blend`) | Hazır bir sahneyi açmak | Yok. DCC'nin bu bilgisayarda kurulu olması yeterli |
| **FBX / Alembic dışa aktar** | DCC başka bilgisayardaysa | Yok. DCC'nin kendi dışa aktarıcısını kullan |

Üç yolda da şunlar gelir:

- geometri, nesne hiyerarşisi ve adlar;
- malzemeler ve dokular (gömülü);
- **kameralar**: sabit olanlar Kayıtlı kameralara, animasyonlu olanlar zaman çizelgesine gider;
- **ışıklar**: nokta, spot, yönlü ve alan ışıkları.

## Canlı bağlantı

PhotonEngine açıkken DCC'deki **Gönder** düğmesi şöyle çalışır:

1. Sahneyi FBX'e aktarır: dokular gömülü, kameralar, ışıklar ve pişirilmiş animasyon dahil, Y yukarı.
2. Dosyayı PhotonEngine'e iletir.
3. Uygulama kapalıysa onu başlatır.

**Aynı sahne yeniden gönderilince model yerinde güncellenir:**

- PhotonEngine'de atadığınız malzemeler parça adına göre korunur.
- Modeli taşıdıysanız konumu korunur.
- Aynı adlı kameralar güncellenir.
- Işıklara dokunulmaz; PhotonEngine'de ayarladığınız gibi kalır.

Açıp kapatmak için PhotonEngine'de **Dosya → Canlı bağlantı** kullanılır.

### 3ds Max (2021 ve sonrası)

1. `3dsmax/photon_engine_livelink.py` ve `3dsmax/photon_engine_livelink.ms` dosyalarını Max'in kullanıcı betik klasörüne kopyalayın. Klasörü MAXScript dinleyicisinde `getDir #userScripts` yazarak bulabilirsiniz.
2. **Scripting → Run Script →** `photon_engine_livelink.ms` ile bir kez çalıştırın.
3. **Customize → Customize User Interface →** *Category: PhotonEngine* altındaki iki eylemi araç çubuğuna ya da bir menüye sürükleyin:
   - *Photon: Seçimi gönder*
   - *Photon: Sahneyi gönder*
4. İsteğe bağlı: her Ctrl+S'de otomatik göndermek için MAXScript dinleyicisinde şunu çalıştırın:
   `python.Execute "photon_send_on_save(True)"`

### Maya (2020 ve sonrası)

1. `maya/photon_engine_livelink.py` dosyasını `Belgeler/maya/scripts` klasörüne kopyalayın.
2. Script Editor'ün Python sekmesinde bir kez çalıştırın:
   ```python
   import photon_engine_livelink as pe
   pe.photon_install_shelf()      # "PhotonEngine" rafı: Seçimi gönder / Sahneyi gönder
   pe.photon_send_on_save(True)   # isteğe bağlı: her kayıtta otomatik gönder
   ```

### Blender (3.6 ve sonrası, 4.x)

1. **Düzen → Tercihler → Eklentiler → Yükle… →** `blender/photon_engine_livelink.py` dosyasını seçin, ardından *PhotonEngine Canlı Bağlantı* eklentisini etkinleştirin.
2. 3B görünümde **N** tuşuyla kenar çubuğunu açın, **PhotonEngine** sekmesine geçin:
   - *Seçimi gönder* / *Sahneyi gönder*
   - *Kaydedince gönder* (isteğe bağlı)

### PhotonEngine kapalıysa

Betik uygulamayı dosyayla birlikte başlatır. Uygulamanın yeri şu sırayla aranır:

1. `PHOTON_APP` ortam değişkeni
2. `Program Files\PhotonEngine\photon_app.exe`
3. `%LOCALAPPDATA%\Programs\PhotonEngine\photon_app.exe`

Taşınabilir (zip) kurulumda `PHOTON_APP` değişkenine `photon_app.exe`'nin tam yolunu yazın.

## Dosyayı doğrudan açma

`.max`, `.ma`, `.mb` veya `.blend` dosyasını PhotonEngine'e sürükleyin ya da **Dosya → Model içe aktar** ile açın. Dosyayı yazan uygulamanın komut satırı aracı arka planda, pencere açmadan dosyayı açar ve FBX'e aktarır:

| Biçim | Gereken | Araç |
|---|---|---|
| `.max` | 3ds Max | `3dsmaxbatch.exe` |
| `.ma`, `.mb` | Maya | `mayapy` |
| `.blend` | Blender | `blender -b` |

**Neden DCC'nin kurulu olması gerekir?** `.max` ve `.mb` kapalı biçimlerdir; yalnız Autodesk'in kendi yazılımı güvenilir şekilde okuyabilir. `.blend` ise Blender'ın iç veri yapısıdır.

- İlk açılış DCC'nin başlama süresi kadar sürer; 3ds Max'te bu 20–60 saniyedir.
- Sonuç önbelleğe alınır (kullanıcı klasörü / `dcc_cache`). Dosya değişmediyse ikinci açılış anında olur.
- Proje bu dosyayı kaynak olarak hatırlar. DCC'de değiştirip kaydederseniz, proje bir sonraki açılışta yeni hâli alır.

Araçlar standart kurulum klasörlerinde otomatik bulunur (her biri için en yeni sürüm). Bulunamazsa ortam değişkeniyle gösterin:

```
PHOTON_3DSMAXBATCH = C:\Program Files\Autodesk\3ds Max 2025\3dsmaxbatch.exe
PHOTON_MAYAPY      = C:\Program Files\Autodesk\Maya2025\bin\mayapy.exe
PHOTON_BLENDER     = C:\Program Files\Blender Foundation\Blender 4.2\blender.exe
```

## Neler aktarılır, neler aktarılmaz

| | Durum |
|---|---|
| Mesh, hiyerarşi, adlar, örnekleme (instancing) | Tam |
| PBR malzemeler: taban rengi, metal/pürüzlülük, normal, emisyon, cam, vernik, sheen, SSS, ince film | Autodesk Standard Surface, 3ds Max Physical, Arnold, Principled BSDF |
| V-Ray / Corona / Redshift malzemeleri | FBX'e yalnız temel renk ve doku olarak çıkar. PhotonEngine kütüphanesinden malzeme atayın; canlı güncellemede korunur |
| Kamera: konum, hedef, odak uzaklığı, film boyu, animasyon | Tam. Her kare örneklenir; yuvarlanma (roll) yok |
| Işıklar: tür, renk, konum, yön, koni açıları, alan boyutu | Tam. **Şiddet** uygulamalar arasında standart olmadığından oranlar korunur, toplam düzey ölçeklenir |
| Opaklık (alfa kesik) haritaları | Henüz yok. İçe aktarma uyarı verir |
| Deforme olan animasyon (karakter, simülasyon) | İlk kare. Alembic'te istenen kare seçilebilir |

## Kendi paket biçimimiz: `.photonpack`

**Dosya → Paket olarak kaydet** ile sahne tek bir dosya olur. İçinde şunlar bulunur:

- tüm dokular, HDRI ve IES profilleri;
- tüm geometri, PhotonEngine'in kendi ikili biçiminde (FBX, Max veya Maya kaynağı gerekmez);
- kamera animasyonları, ışıklar ve render ayarları.

Başka bilgisayara taşımak, arşivlemek ve paylaşmak için kullanın. Paket bir ZIP'tir. Açarken içerik kullanıcı klasöründeki önbelleğe çıkarılır; aynı paket ikinci kez açılınca yeniden çıkarılmaz.

## Protokol (kendi aracınızı yazmak için)

PhotonEngine çalışırken kullanıcı veri klasörüne `livelink.json` dosyasını yazar:

- Windows: `%APPDATA%\PhotonEngine\livelink.json`
- Linux/macOS: `~/.photon-engine/livelink.json`

İçeriği:

```json
{"port": 47260, "token": "…32 hex…", "app": "PhotonEngine", "version": "0.3.0"}
```

`127.0.0.1:port` adresine TCP ile bağlanın, tek satır JSON gönderin ve tek satır yanıt alın:

```json
{"token": "…", "cmd": "import", "path": "C:/tmp/sahne.fbx", "source": "3dsmax", "name": "Araba", "replace": true}
{"ok": true}
```

`"cmd": "ping"` bağlantıyı denetler.

- **`path`:** mutlak olmalı ve var olan bir dosyayı göstermelidir. Kabul edilenler `.fbx`, `.abc`, `.obj`, `.gltf`, `.glb`, `.max`, `.ma`, `.mb`, `.blend`, `.photon` ve `.photonpack`.
- **Yerinde güncelleme:** aynı `source` + `name` ikilisi yeniden gelirse model yerinde güncellenir. Bunu istemiyorsanız `"replace": false` gönderin.

Ortak Python istemcisi `common/photon_livelink.py` dosyasındadır; DCC betikleri onun birebir kopyasını içerir. İstemciyi değiştirirseniz kopyaları `python integrations/sync_client.py` ile güncelleyin. Testler kopyaların eşit olduğunu denetler.

**Güvenlik:**

- Sunucu yalnız bu bilgisayardan erişilebilir.
- Jeton her açılışta yeniden üretilir ve yalnız sizin kullanıcı hesabınızın okuyabildiği dosyadadır.
- JSON olmayan istekler (ör. bir web sayfasının localhost'a gönderdiği HTTP isteği) yanıtsız kapatılır.
