# smoke_recovery.ps1 — Çökme kurtarmasının gerçek uygulamayla uçtan uca denemesi.
# Pencere ve GPU ister, CI'da çalışmaz (GPU'suz makinede arayüz açılmaz). Kullanıcı verisine
# dokunmaz: APPDATA geçici bir klasöre yönlendirilir.
#   pwsh -File scripts\smoke_recovery.ps1 [-App build\fetch-release\src\app\photon_app.exe]
param([string]$App = 'build\fetch-release\src\app\photon_app.exe')
$ErrorActionPreference = 'Stop'

$App = (Resolve-Path -LiteralPath $App).Path
$sandbox = Join-Path ([IO.Path]::GetTempPath()) "photon_recovery_$PID"
New-Item -ItemType Directory -Force -Path $sandbox | Out-Null
$env:APPDATA = $sandbox
$recovery = Join-Path $sandbox 'PhotonEngine\autosave\recovery.photon'
$saved = Join-Path $sandbox 'kurtarilan.photon'

function Fail([string]$msg) {
    Write-Host "BAŞARISIZ: $msg"
    exit 1
}
function Run([string[]]$appArgs) {
    return (Start-Process -FilePath $App -ArgumentList $appArgs -PassThru -Wait).ExitCode
}

# 1. Belge değişir (sahneye Simit eklenir), otomatik kayıt diske iner, süreç temizlik yapmadan
#    ölür. Temiz çıkış kurtarma dosyasını silerdi; çıkış kodu 3 çökme benzetiminin işareti.
$code = Run @('--screenshot', (Join-Path $sandbox 'a.png'), '--ui', 'autosave')
if ($code -ne 3) { Fail "çökme benzetimi çalışmadı (çıkış kodu $code)" }
if (-not (Test-Path -LiteralPath $recovery)) { Fail 'otomatik kayıt dosyası yazılmadı' }

# 2. Yeni oturum kurtarır ve kaydeder: Simit geri gelmiş olmalı, kurtarma dosyası kalkmalı.
$code = Run @('--screenshot', (Join-Path $sandbox 'b.png'), '--ui', 'recovered', '--save-project', $saved)
if ($code -ne 0) { Fail "kurtarılan oturum hata verdi (çıkış kodu $code)" }
if (-not (Test-Path -LiteralPath $saved)) { Fail 'kurtarılan proje kaydedilmedi' }
if (-not (Select-String -LiteralPath $saved -Pattern 'Simit' -Quiet)) { Fail 'kurtarılan sahnede değişiklik yok' }
if (Test-Path -LiteralPath $recovery) { Fail 'kaydettikten sonra kurtarma dosyası silinmedi' }

# 3. Cevaplanmamış soru dosyayı silmez; kurtarılıp temiz çıkılınca dosya kalmaz.
$code = Run @('--screenshot', (Join-Path $sandbox 'c.png'), '--ui', 'autosave')
if ($code -ne 3) { Fail "ikinci çökme benzetimi çalışmadı (çıkış kodu $code)" }
$code = Run @('--screenshot', (Join-Path $sandbox 'd.png'), '--ui', 'recovery')
if ($code -ne 0) { Fail "soruyla çıkış hata verdi (çıkış kodu $code)" }
if (-not (Test-Path -LiteralPath $recovery)) { Fail 'cevaplanmamış soruda kurtarma dosyası silindi' }
$code = Run @('--screenshot', (Join-Path $sandbox 'e.png'), '--ui', 'recovered')
if ($code -ne 0) { Fail "temiz çıkış hata verdi (çıkış kodu $code)" }
if (Test-Path -LiteralPath $recovery) { Fail 'temiz çıkışta kurtarma dosyası kaldı' }

Remove-Item -Recurse -Force -LiteralPath $sandbox
Write-Host 'Kurtarma denemesi geçti'
