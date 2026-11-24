#!/usr/bin/env python3
"""
espGIF Window Streamer (PC/Windows)
Captures a specific window and streams RGB565 frames to the ESP32 over WebSocket.

Usage:
    python stream_window.py                     # list windows, pick one
    python stream_window.py --window "Discord"  # match by title substring
    python stream_window.py --host 192.168.1.50 # skip auto-discovery
    python stream_window.py --fps 15            # target framerate

Requirements:
    pip install websocket-client pillow mss pywin32
    (numpy optional but recommended for speed)
"""

import argparse
import ctypes
import ctypes.wintypes
import socket
import struct
import sys
import time

try:
    import websocket
except ImportError:
    sys.exit("Missing: pip install websocket-client")

try:
    from PIL import Image
except ImportError:
    sys.exit("Missing: pip install pillow")

try:
    import mss
except ImportError:
    sys.exit("Missing: pip install mss")

try:
    import win32gui
    import win32con
except ImportError:
    sys.exit("Missing: pip install pywin32")

DISPLAY_W = 172
DISPLAY_H = 320
FRAME_SIZE = DISPLAY_W * DISPLAY_H * 2
UDP_PORT = 4210
DISCOVERY_TIMEOUT = 3.0


def discover_esp32():
    """Broadcast UDP to find the ESP32 on the network."""
    print("Searching for espGIF on the network...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(DISCOVERY_TIMEOUT)

    try:
        sock.sendto(b"ESPGIF?", ("<broadcast>", UDP_PORT))
        data, addr = sock.recvfrom(256)
        reply = data.decode("utf-8", errors="ignore")
        if reply.startswith("ESPGIF:"):
            ws_url = reply[7:]
            print(f"Found espGIF at {addr[0]} -> {ws_url}")
            return ws_url
    except socket.timeout:
        pass
    finally:
        sock.close()

    for subnet in ["192.168.1.255", "192.168.0.255", "10.0.0.255"]:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            sock.settimeout(DISCOVERY_TIMEOUT)
            sock.sendto(b"ESPGIF?", (subnet, UDP_PORT))
            data, addr = sock.recvfrom(256)
            reply = data.decode("utf-8", errors="ignore")
            if reply.startswith("ESPGIF:"):
                ws_url = reply[7:]
                print(f"Found espGIF at {addr[0]} -> {ws_url}")
                return ws_url
        except socket.timeout:
            continue
        finally:
            sock.close()

    return None


def list_windows():
    """Return list of (hwnd, title) for visible windows with titles."""
    windows = []

    def enum_cb(hwnd, _):
        if not win32gui.IsWindowVisible(hwnd):
            return
        title = win32gui.GetWindowText(hwnd)
        if not title or title == "Program Manager":
            return
        windows.append((hwnd, title))

    win32gui.EnumWindows(enum_cb, None)
    return windows


def pick_window(match=None):
    """Interactive window picker or match by title substring."""
    windows = list_windows()
    if not windows:
        sys.exit("No visible windows found")

    if match:
        match_lower = match.lower()
        matches = [(h, t) for h, t in windows if match_lower in t.lower()]
        if not matches:
            sys.exit(f"No window matching '{match}'")
        if len(matches) == 1:
            print(f"Matched: {matches[0][1]}")
            return matches[0][0]
        # Multiple matches — show them
        windows = matches

    print("\nVisible windows:")
    print("-" * 60)
    for i, (hwnd, title) in enumerate(windows):
        short = title[:55] + "..." if len(title) > 55 else title
        print(f"  {i:3d}) {short}")
    print("-" * 60)

    while True:
        try:
            choice = input(f"Pick a window [0-{len(windows)-1}]: ").strip()
            idx = int(choice)
            if 0 <= idx < len(windows):
                return windows[idx][0]
        except (ValueError, EOFError):
            pass
        print("Invalid choice, try again")


def get_window_rect(hwnd):
    """Get window client area position and size on screen."""
    try:
        # Try DwmGetWindowAttribute for accurate bounds (excludes shadow)
        dwm = ctypes.windll.dwmapi
        rect = ctypes.wintypes.RECT()
        DWMWA_EXTENDED_FRAME_BOUNDS = 9
        hr = dwm.DwmGetWindowAttribute(
            hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
            ctypes.byref(rect), ctypes.sizeof(rect))
        if hr == 0:
            return rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top
    except Exception:
        pass
    # Fallback
    r = win32gui.GetWindowRect(hwnd)
    return r[0], r[1], r[2] - r[0], r[3] - r[1]


def make_converter():
    """Return the fastest available RGB-to-RGB565 converter."""
    try:
        import numpy as np

        def convert(img):
            img = img.resize((DISPLAY_W, DISPLAY_H), Image.LANCZOS)
            arr = np.array(img, dtype=np.uint16)
            r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            rgb565 = ((rgb565 >> 8) | (rgb565 << 8)) & 0xFFFF
            return rgb565.astype(np.uint16).tobytes()

        print("Using numpy for fast RGB565 conversion")
        return convert
    except ImportError:
        pass

    def convert(img):
        img = img.resize((DISPLAY_W, DISPLAY_H), Image.LANCZOS)
        pixels = img.tobytes()
        buf = bytearray(FRAME_SIZE)
        for i in range(DISPLAY_W * DISPLAY_H):
            off = i * 3
            r, g, b = pixels[off], pixels[off + 1], pixels[off + 2]
            val = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            struct.pack_into(">H", buf, i * 2, val)
        return bytes(buf)

    print("numpy not found, using pure Python (slower — pip install numpy)")
    return convert


def main():
    parser = argparse.ArgumentParser(description="espGIF Window Streamer")
    parser.add_argument("--host", help="ESP32 IP (skip discovery)")
    parser.add_argument("--port", type=int, default=81, help="WebSocket port")
    parser.add_argument("--fps", type=int, default=10, help="Target FPS")
    parser.add_argument("--window", "-w", help="Window title to match")
    parser.add_argument("--list", "-l", action="store_true", help="List windows and exit")
    args = parser.parse_args()

    if args.list:
        for hwnd, title in list_windows():
            print(f"  {title}")
        return

    # Pick window
    hwnd = pick_window(args.window)
    title = win32gui.GetWindowText(hwnd)
    print(f"Streaming: {title}")

    # Find ESP32
    if args.host:
        ws_url = f"ws://{args.host}:{args.port}/stream"
    else:
        ws_url = discover_esp32()
        if not ws_url:
            sys.exit("Could not find espGIF on the network.\n"
                     "Use --host <ip> to specify manually.")

    convert = make_converter()

    # Connect
    print(f"Connecting to {ws_url}...")
    ws = websocket.WebSocket()
    ws.settimeout(5)
    try:
        ws.connect(ws_url)
    except Exception as e:
        sys.exit(f"Connection failed: {e}")
    print("Connected! Streaming... (Ctrl+C to stop)")

    frame_interval = 1.0 / args.fps
    frames_sent = 0
    start_time = time.time()

    try:
        with mss.mss() as sct:
            while True:
                t0 = time.time()

                # Check window still exists
                if not win32gui.IsWindow(hwnd):
                    print("\nWindow closed.")
                    break

                # Bring window rect (it may have moved/resized)
                x, y, w, h = get_window_rect(hwnd)
                if w <= 0 or h <= 0:
                    # Window minimized — wait
                    time.sleep(0.5)
                    continue

                region = {"left": x, "top": y, "width": w, "height": h}

                # Capture
                shot = sct.grab(region)
                img = Image.frombytes("RGB", shot.size, shot.rgb)

                # Convert and send
                data = convert(img)
                try:
                    ws.send_binary(data)
                except Exception as e:
                    print(f"\nConnection lost: {e}")
                    break

                frames_sent += 1
                elapsed = time.time() - t0
                sleep_time = frame_interval - elapsed
                if sleep_time > 0:
                    time.sleep(sleep_time)

                if frames_sent % (args.fps * 5) == 0:
                    actual_fps = frames_sent / (time.time() - start_time)
                    print(f"  {frames_sent} frames | {actual_fps:.1f} FPS | "
                          f"{elapsed*1000:.0f}ms/frame | {w}x{h}")

    except KeyboardInterrupt:
        print(f"\nStopped. Sent {frames_sent} frames.")
    finally:
        ws.close()


if __name__ == "__main__":
    main()
