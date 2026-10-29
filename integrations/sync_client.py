"""DCC betiklerindeki istemci bloğunu integrations/common/photon_livelink.py ile eşitler.

Kullanım: python integrations/sync_client.py
Her betikte ya PLACEHOLDER_CLIENT satırı ya da "--- photon_livelink istemci başlangıcı/bitişi ---"
işaretleri arasındaki blok vardır; ikisi de ortak istemcinin güncel kopyasıyla değiştirilir.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
BEGIN = "# --- photon_livelink istemci başlangıcı ---"
END = "# --- photon_livelink istemci bitişi ---"
TARGETS = ["blender/photon_engine_livelink.py", "maya/photon_engine_livelink.py", "3dsmax/photon_engine_livelink.py"]


def main():
    with open(os.path.join(HERE, "common", "photon_livelink.py"), encoding="utf-8") as f:
        client = f.read().strip("\n")
    block = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.S)
    for rel in TARGETS:
        path = os.path.join(HERE, rel)
        with open(path, encoding="utf-8") as f:
            text = f.read()
        if "PLACEHOLDER_CLIENT" in text:
            text = text.replace("PLACEHOLDER_CLIENT", client, 1)
        else:
            text = block.sub(lambda _m: client, text, count=1)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("eşitlendi:", rel)


if __name__ == "__main__":
    main()
