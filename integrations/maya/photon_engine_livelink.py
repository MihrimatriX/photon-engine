# PhotonEngine canlı bağlantı — Maya (2020 ve sonrası, Python 3).
#
# Kurulum: bu dosyayı belgelerinizdeki maya/scripts klasörüne kopyalayın, Maya'da Script Editor'ün
# Python sekmesinde bir kez çalıştırın:
#     import photon_engine_livelink as pe; pe.photon_install_shelf()
# "PhotonEngine" rafında iki düğme belirir: Seçimi gönder / Sahneyi gönder. Kayıtta otomatik gönderim:
#     pe.photon_send_on_save(True)
# Dışa aktarma: FBX (fbxmaya eklentisi), dokular gömülü, kameralar, ışıklar, pişirilmiş animasyon, Y yukarı.
import maya.cmds as cmds  # noqa: E402
import maya.mel as mel  # noqa: E402

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

_photon_save_job = None


def _scene_name():
    path = cmds.file(q=True, sceneName=True)
    if not path:
        return "Maya sahnesi"
    return _pl_os.path.splitext(_pl_os.path.basename(path))[0]


def _export(path, selection_only):
    if not cmds.pluginInfo("fbxmaya", q=True, loaded=True):
        cmds.loadPlugin("fbxmaya")
    start = int(cmds.playbackOptions(q=True, minTime=True))
    end = int(cmds.playbackOptions(q=True, maxTime=True))
    for command in (
        "FBXResetExport",
        "FBXExportEmbeddedTextures -v true",
        "FBXExportCameras -v true",
        "FBXExportLights -v true",
        "FBXExportSmoothingGroups -v true",
        "FBXExportTangents -v true",
        "FBXExportUpAxis y",
        "FBXExportInAscii -v false",
        "FBXExportBakeComplexAnimation -v true",
        "FBXExportBakeComplexStart -v %d" % start,
        "FBXExportBakeComplexEnd -v %d" % end,
        "FBXExportBakeComplexStep -v 1",
    ):
        mel.eval(command)
    mel.eval('FBXExport -f "%s"%s' % (path.replace("\\", "/"), " -s" if selection_only else ""))


def photon_send(selection_only=True):
    if selection_only and not cmds.ls(selection=True):
        cmds.warning("PhotonEngine: seçili nesne yok")
        return False
    name = _scene_name() + (" (seçim)" if selection_only else "")
    path = photon_export_path("maya", name)
    _export(path, selection_only)
    ok, message = photon_send_file(path, "maya", name)
    if ok:
        cmds.inViewMessage(amg=message, pos="topCenter", fade=True)
    else:
        cmds.warning(message)
    return ok


def photon_install_shelf():
    shelf = "PhotonEngine"
    top = mel.eval("$tmp = $gShelfTopLevel")
    if cmds.shelfLayout(shelf, exists=True):
        cmds.deleteUI(shelf)
    cmds.shelfLayout(shelf, parent=top)
    for label, selection in (("Seçimi gönder", True), ("Sahneyi gönder", False)):
        cmds.shelfButton(
            parent=shelf,
            label=label,
            annotation="PhotonEngine: " + label,
            imageOverlayLabel="PE" + ("·S" if selection else "·A"),
            image1="pythonFamily.png",
            sourceType="python",
            command="import photon_engine_livelink as pe; pe.photon_send(%s)" % selection,
        )


def photon_send_on_save(enabled=True):
    """Her kayıtta sahneyi gönder (PhotonEngine'de yerinde güncellenir)."""
    global _photon_save_job
    if _photon_save_job is not None and cmds.scriptJob(exists=_photon_save_job):
        cmds.scriptJob(kill=_photon_save_job, force=True)
        _photon_save_job = None
    if enabled:
        _photon_save_job = cmds.scriptJob(event=["SceneSaved", lambda: photon_is_running() and photon_send(False)])
