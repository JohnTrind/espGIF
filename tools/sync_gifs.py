"""
Sync all GIFs from a folder to the ESP32 espGIF device.
Uploads new/changed files, deletes ones not in the folder.

Usage: python sync_gifs.py [folder] [esp_ip]
Defaults: folder=./gifs_ready  ip=espgif.local
"""

import sys
import os
import json
import time
from pathlib import Path

try:
    import requests
except ImportError:
    print("Installing requests...")
    os.system(f"{sys.executable} -m pip install requests")
    import requests


def get_esp_gifs(base_url: str) -> dict:
    try:
        r = requests.get(f"{base_url}/api/gifs", timeout=10)
        r.raise_for_status()
        return {g["name"]: g["size"] for g in r.json()}
    except Exception as e:
        print(f"Error connecting to device: {e}")
        return None


def upload_gif(base_url: str, filepath: Path) -> bool:
    try:
        with open(filepath, "rb") as f:
            r = requests.post(
                f"{base_url}/api/upload",
                files={"file": (filepath.name, f, "image/gif")},
                timeout=120,
            )
        return r.status_code == 200
    except Exception as e:
        print(f"error: {e}")
        return False


def delete_gif(base_url: str, name: str) -> bool:
    try:
        r = requests.delete(f"{base_url}/api/gifs", params={"name": name}, timeout=10)
        return r.status_code == 200
    except Exception as e:
        print(f"error: {e}")
        return False


def select_gif(base_url: str, name: str) -> bool:
    try:
        r = requests.post(
            f"{base_url}/api/select",
            json={"name": name},
            timeout=10,
        )
        return r.status_code == 200
    except Exception as e:
        print(f"error: {e}")
        return False


def main():
    folder = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("gifs_ready")
    host = sys.argv[2] if len(sys.argv) > 2 else "espgif.local"
    base_url = f"http://{host}"

    if not folder.exists():
        print(f"Folder not found: {folder}")
        return

    local_gifs = {}
    for f in sorted(folder.glob("*.[gG][iI][fF]")):
        local_gifs[f.name] = f.stat().st_size

    if not local_gifs:
        print(f"No GIFs in {folder}")
        return

    print(f"Connecting to {base_url}...")
    remote_gifs = get_esp_gifs(base_url)
    if remote_gifs is None:
        return

    print(f"Local: {len(local_gifs)} GIFs | Device: {len(remote_gifs)} GIFs\n")

    uploaded = 0
    for name, size in local_gifs.items():
        remote_size = remote_gifs.get(name)
        if remote_size is not None and remote_size == size:
            print(f"  SKIP    {name} (same size)")
            continue

        action = "UPDATE" if name in remote_gifs else "UPLOAD"
        filepath = folder / name
        print(f"  {action:6s}  {name} ({size / 1024:.1f} KB)...", end=" ", flush=True)
        if upload_gif(base_url, filepath):
            print("OK")
            uploaded += 1
            time.sleep(0.5)  # give ESP32 time to settle
        else:
            print("FAILED")

    deleted = 0
    for name in remote_gifs:
        if name not in local_gifs:
            print(f"  DELETE  {name}...", end=" ", flush=True)
            if delete_gif(base_url, name):
                print("OK")
                deleted += 1
            else:
                print("FAILED")

    print(f"\nDone! Uploaded: {uploaded}, Deleted: {deleted}")

    if uploaded > 0:
        first = sorted(local_gifs.keys())[0]
        print(f"Playing: {first}")
        select_gif(base_url, first)


if __name__ == "__main__":
    main()
