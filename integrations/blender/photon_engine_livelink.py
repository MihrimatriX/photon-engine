# PhotonEngine canlı bağlantı — Blender eklentisi (3.6 ve sonrası, 4.x).
#
# Kurulum: Düzen → Tercihler → Eklentiler → Yükle… → bu dosya → "PhotonEngine Canlı Bağlantı"yı etkinleştir.
# Kullanım: 3B görünümde N kenar çubuğu → PhotonEngine sekmesi → "Seçimi gönder" / "Sahneyi gönder".
#   "Kaydedince gönder" açıksa her Ctrl+S'de sahne PhotonEngine'de yerinde güncellenir (malzemeler korunur).
# Dışa aktarma: FBX, dokular gömülü, kameralar, ışıklar ve pişirilmiş animasyon; Y yukarı, birimler uygulanmış.
bl_info = {
    "name": "PhotonEngine Canlı Bağlantı",
    "author": "PhotonEngine",
    "version": (1, 0, 0),
    "blender": (3, 6, 0),
    "location": "3B Görünüm > Kenar çubuğu (N) > PhotonEngine",
    "description": "Sahneyi tek tıkla PhotonEngine'e gönderir (FBX: dokular, kameralar, ışıklar, animasyon)",
    "category": "Import-Export",
}

import bpy  # noqa: E402
from bpy.app.handlers import persistent  # noqa: E402

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


_photon_status = [0.0, False]  # (zaman, bağlı mı): panel her çizimde soket açmasın


def _cached_is_running():
    now = _pl_time.time()
    if now - _photon_status[0] > 3.0:
        _photon_status[0] = now
        _photon_status[1] = photon_is_running()
    return _photon_status[1]


def _scene_name():
    path = bpy.data.filepath
    return bpy.path.display_name_from_filepath(path) if path else "Blender sahnesi"


def photon_send(context, selection_only):
    name = _scene_name() + (" (seçim)" if selection_only else "")
    path = photon_export_path("blender", name)
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=selection_only,
        object_types={"MESH", "CURVE", "SURFACE", "META", "FONT", "CAMERA", "LIGHT", "EMPTY", "OTHER"},
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        axis_forward="-Z",
        axis_up="Y",
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        use_tspace=True,
        add_leaf_bones=False,
        path_mode="COPY",
        embed_textures=True,
        bake_anim=True,
        bake_anim_use_all_actions=False,
        bake_anim_use_nla_strips=False,
        bake_anim_step=1.0,
        bake_anim_simplify_factor=0.0,
    )
    return photon_send_file(path, "blender", name)


class PHOTON_OT_send(bpy.types.Operator):
    """Sahneyi ya da seçimi PhotonEngine'e gönderir"""
    bl_idname = "photon.send"
    bl_label = "PhotonEngine'e gönder"
    bl_options = {"REGISTER"}

    selection_only: bpy.props.BoolProperty(name="Yalnız seçim", default=True)

    def execute(self, context):
        if self.selection_only and not context.selected_objects:
            self.report({"WARNING"}, "Seçili nesne yok")
            return {"CANCELLED"}
        ok, message = photon_send(context, self.selection_only)
        self.report({"INFO"} if ok else {"ERROR"}, message)
        return {"FINISHED"} if ok else {"CANCELLED"}


class PHOTON_PT_panel(bpy.types.Panel):
    bl_label = "PhotonEngine"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "PhotonEngine"

    def draw(self, context):
        layout = self.layout
        running = _cached_is_running()
        layout.label(text="Bağlı" if running else "PhotonEngine kapalı (gönderince açılır)",
                     icon="CHECKMARK" if running else "INFO")
        layout.operator(PHOTON_OT_send.bl_idname, text="Seçimi gönder", icon="EXPORT").selection_only = True
        layout.operator(PHOTON_OT_send.bl_idname, text="Sahneyi gönder", icon="SCENE_DATA").selection_only = False
        layout.prop(context.scene, "photon_send_on_save")


@persistent
def _photon_on_save(_dummy):
    scene = bpy.context.scene
    if scene is not None and getattr(scene, "photon_send_on_save", False) and photon_is_running():
        photon_send(bpy.context, False)


def register():
    bpy.utils.register_class(PHOTON_OT_send)
    bpy.utils.register_class(PHOTON_PT_panel)
    bpy.types.Scene.photon_send_on_save = bpy.props.BoolProperty(
        name="Kaydedince gönder",
        description="Her kayıtta sahne PhotonEngine'de yerinde güncellenir (atanmış malzemeler korunur)",
        default=False,
    )
    bpy.app.handlers.save_post.append(_photon_on_save)


def unregister():
    if _photon_on_save in bpy.app.handlers.save_post:
        bpy.app.handlers.save_post.remove(_photon_on_save)
    del bpy.types.Scene.photon_send_on_save
    bpy.utils.unregister_class(PHOTON_PT_panel)
    bpy.utils.unregister_class(PHOTON_OT_send)
