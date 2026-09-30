# USB ile yuklerken modulun MAC'ini okur, yukleme basariliysa platformio.ini'ye
# o modul icin kablosuz (OTA) ortam ekler: [env:esp01_xxxxxx_ota]
# MAC yuklemeden ONCE okunur: reset hatti olmayan programlayicida modul yuklemeden
# sonra bootloader'dan cikiyor, okuma basarisiz olurdu.
Import("env")
import os
import re
import subprocess

state = {}


def read_mac(source, target, env):
    if env.GetProjectOption("upload_protocol", "esptool") == "espota":
        return
    esptool = os.path.join(env.PioPlatform().get_package_dir("tool-esptoolpy"), "esptool.py")
    cmd = [env.subst("$PYTHONEXE"), esptool, "--port", env.subst("$UPLOAD_PORT"),
           "--no-stub", "--after", "no_reset", "read_mac"]
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout
    except Exception as e:
        print("[auto-ota] MAC okunamadi:", e)
        return
    m = re.search(r"MAC:\s*([0-9a-fA-F:]{17})", out)
    if m:
        state["mac"] = m.group(1).replace(":", "").lower()
        print("[auto-ota] MAC:", state["mac"])
    else:
        print("[auto-ota] MAC okunamadi (modul flas modunda mi?)")


def add_ota_env(source, target, env):
    mac = state.get("mac")
    if not mac:
        return
    suffix = mac[6:]
    # Kart turune gore: ESP-01 "esp01-", C3 "c3-" (firmware'deki HOST_PREFIX ile ayni)
    prefix = env.GetProjectOption("custom_host_prefix", "esp01-")
    base_env = env.subst("$PIOENV")
    name = "%s%s_ota" % (prefix.replace("-", "_"), suffix)
    host = "%s%s.local" % (prefix, suffix)
    ini = os.path.join(env.subst("$PROJECT_DIR"), "platformio.ini")
    with open(ini, encoding="utf-8") as f:
        txt = f.read()
    if host in txt:
        print("[auto-ota] %s zaten kayitli" % host)
        return
    with open(ini, "a", encoding="utf-8") as f:
        f.write("\n[env:%s]\nextends = env:%s\nupload_protocol = espota\nupload_port = %s\n"
                % (name, base_env, host))
    print("[auto-ota] Eklendi: env:%s -> %s" % (name, host))


env.AddPreAction("upload", read_mac)
env.AddPostAction("upload", add_ota_env)
