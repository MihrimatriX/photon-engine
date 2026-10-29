# --- photon_livelink istemci başlangıcı ---
# PhotonEngine canlı bağlantı istemcisi (saf Python 3, bağımlılık yok). 3ds Max, Maya ve Blender
# betikleri bu bloğun birebir kopyasını içerir (tek dosyalık kurulum); asıl kopya
# integrations/common/photon_livelink.py, eşitliği tests/test_live_link.cpp denetler.
#
# Uygulama çalışırken kullanıcı veri klasörüne livelink.json yazar: {"port": ..., "token": ...}.
# İstemci 127.0.0.1:port'a bağlanır, tek satır JSON gönderir, tek satır yanıt alır. Uygulama
# kapalıysa dosyayla birlikte başlatılır (PHOTON_APP ortam değişkeni ya da standart kurulum yeri).
import json as _pl_json
import os as _pl_os
import socket as _pl_socket
import subprocess as _pl_subprocess
import sys as _pl_sys
import tempfile as _pl_tempfile
import time as _pl_time


def photon_info_path():
    if _pl_sys.platform == "win32":
        base = _pl_os.environ.get("APPDATA") or _pl_os.path.expanduser("~")
        return _pl_os.path.join(base, "PhotonEngine", "livelink.json")
    return _pl_os.path.join(_pl_os.path.expanduser("~"), ".photon-engine", "livelink.json")


def photon_request(payload, timeout=5.0):
    """Tek istek gönderir, yanıtı (dict) döndürür. Uygulama kapalıysa OSError fırlatır."""
    with open(photon_info_path(), "r", encoding="utf-8") as f:
        info = _pl_json.load(f)
    message = dict(payload)
    message["token"] = info["token"]
    with _pl_socket.create_connection(("127.0.0.1", int(info["port"])), timeout=timeout) as s:
        s.sendall((_pl_json.dumps(message) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n") and len(data) < 65536:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    return _pl_json.loads(data.decode("utf-8") or "{}")


def photon_is_running():
    try:
        return bool(photon_request({"cmd": "ping"}, timeout=1.0).get("ok"))
    except (OSError, ValueError, KeyError):
        return False


def photon_find_app():
    """photon_app yürütülebilirinin yolu: PHOTON_APP, sonra standart kurulum yerleri."""
    env = _pl_os.environ.get("PHOTON_APP")
    if env and _pl_os.path.isfile(env):
        return env
    candidates = []
    if _pl_sys.platform == "win32":
        for var in ("ProgramFiles", "LOCALAPPDATA"):
            root = _pl_os.environ.get(var)
            if root:
                candidates.append(_pl_os.path.join(root, "PhotonEngine", "photon_app.exe"))
                candidates.append(_pl_os.path.join(root, "Programs", "PhotonEngine", "photon_app.exe"))
    else:
        for d in _pl_os.environ.get("PATH", "").split(_pl_os.pathsep):
            candidates.append(_pl_os.path.join(d, "photon_app"))
    for c in candidates:
        if _pl_os.path.isfile(c):
            return c
    return None


def photon_export_path(source, name):
    """Dışa aktarma için geçici dosya. Her gönderim yeni ad alır (uygulama önceki dosyayı okurken
    üzerine yazılmasın); aynı kaynağın eski dosyalarından son beşi tutulur."""
    folder = _pl_os.path.join(_pl_tempfile.gettempdir(), "photon_livelink")
    _pl_os.makedirs(folder, exist_ok=True)
    safe = "".join(c if c.isalnum() or c in "-_" else "_" for c in name)[:60] or "sahne"
    prefix = "%s_%s_" % (source, safe)
    old = sorted(f for f in _pl_os.listdir(folder) if f.startswith(prefix))
    for f in old[:-4]:
        try:
            _pl_os.remove(_pl_os.path.join(folder, f))
        except OSError:
            pass
    return _pl_os.path.join(folder, "%s%d.fbx" % (prefix, int(_pl_time.time() * 1000)))


def photon_send_file(path, source, name, replace=True):
    """Dosyayı uygulamaya gönderir. (başarılı mı, mesaj) döndürür."""
    path = _pl_os.path.abspath(path)
    try:
        reply = photon_request({"cmd": "import", "path": path, "source": source, "name": name, "replace": replace})
        if reply.get("ok"):
            return True, "PhotonEngine'e gönderildi: " + name
        return False, "PhotonEngine reddetti: " + str(reply.get("error", "?"))
    except (OSError, ValueError, KeyError):
        exe = photon_find_app()
        if not exe:
            return False, ("PhotonEngine çalışmıyor ve bulunamadı. Uygulamayı açın ya da PHOTON_APP ortam "
                           "değişkenine photon_app yolunu yazın.")
        _pl_subprocess.Popen([exe, path])
        return True, "PhotonEngine başlatıldı: " + name
# --- photon_livelink istemci bitişi ---
