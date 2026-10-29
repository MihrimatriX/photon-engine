-- PhotonEngine canlı bağlantı — 3ds Max eylemleri (MacroScript).
-- photon_engine_livelink.py ile aynı klasörde (getDir #userScripts) olmalıdır. Bir kez çalıştırın;
-- Customize → Customize User Interface → Kategori "PhotonEngine" altından araç çubuğuna sürükleyin.
macroScript PhotonSendSelection category:"PhotonEngine" buttonText:"Photon: Seçimi gönder" tooltip:"Seçimi PhotonEngine'e gönder"
(
    python.ExecuteFile ((getDir #userScripts) + "\\photon_engine_livelink.py")
    python.Execute "photon_send(True)"
)
macroScript PhotonSendScene category:"PhotonEngine" buttonText:"Photon: Sahneyi gönder" tooltip:"Tüm sahneyi PhotonEngine'e gönder"
(
    python.ExecuteFile ((getDir #userScripts) + "\\photon_engine_livelink.py")
    python.Execute "photon_send(False)"
)
