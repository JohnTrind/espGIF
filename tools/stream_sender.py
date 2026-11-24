#!/usr/bin/env python3
"""
espGIF Stream Sender
Captures the screen and streams RGB565 frames to the ESP32 over WebSocket.

Usage:
    python3 stream_sender.py                  # auto-discover ESP32
    python3 stream_sender.py --host 192.168.1.50  # manual IP
    python3 stream_sender.py --fps 15         # target framerate
    python3 stream_sender.py --region 0,0,500,500  # capture region

Requirements:
    pip install websocket-client pillow mss
"""

import argparse
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

DISPLAY_W = 172
DISPLAY_H = 320
FRAME_SIZE = DISPLAY_W * DISPLAY_H * 2  # RGB565
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

    # Try directed broadcast on common subnets
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


def rgb888_to_rgb565(r, g, b):
    """Convert 8-bit RGB to 16-bit RGB565 (big-endian for ST7789)."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_rgb565(img):
    """Convert a PIL Image to RGB565 bytes (big-endian)."""
    img = img.resize((DISPLAY_W, DISPLAY_H), Image.LANCZOS)
    pixels = img.tobytes()  # RGB888
    buf = bytearray(FRAME_SIZE)
    for i in range(DISPLAY_W * DISPLAY_H):
        off = i * 3
        r, g, b = pixels[off], pixels[off + 1], pixels[off + 2]
        val = rgb888_to_rgb565(r, g, b)
        struct.pack_into(">H", buf, i * 2, val)
    return bytes(buf)


def image_to_rgb565_fast(img):
    """Faster RGB565 conversion using numpy if available."""
    try:
        import numpy as np
        img = img.resize((DISPLAY_W, DISPLAY_H), Image.LANCZOS)
        arr = np.array(img, dtype=np.uint16)
        r = arr[:, :, 0]
        g = arr[:, :, 1]
        b = arr[:, :, 2]
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        # Big-endian byte swap
        rgb565 = ((rgb565 >> 8) | (rgb565 << 8)) & 0xFFFF
        return rgb565.astype(np.uint8.__class__).tobytes()
    except ImportError:
        return image_to_rgb565(img)


def try_numpy_convert():
    """Check if numpy is available and return the fast converter."""
    try:
        import numpy as np

        def convert(img):
            img = img.resize((DISPLAY_W, DISPLAY_H), Image.LANCZOS)
            arr = np.array(img, dtype=np.uint16)
            r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
            rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            # Swap to big-endian
            rgb565 = ((rgb565 >> 8) | (rgb565 << 8)) & 0xFFFF
            return rgb565.astype(np.uint16).tobytes()

        print("Using numpy for fast RGB565 conversion")
        return convert
    except ImportError:
        print("numpy not found, using pure Python conversion (slower)")
        return image_to_rgb565


def parse_region(s):
    """Parse 'x,y,w,h' region string."""
    parts = [int(x) for x in s.split(",")]
    if len(parts) != 4:
        raise ValueError("Region must be x,y,w,h")
    return {"left": parts[0], "top": parts[1],
            "width": parts[2], "height": parts[3]}


def main():
    parser = argparse.ArgumentParser(description="espGIF Stream Sender")
    parser.add_argument("--host", help="ESP32 IP address (skip discovery)")
    parser.add_argument("--port", type=int, default=81, help="WebSocket port")
    parser.add_argument("--fps", type=int, default=10, help="Target FPS")
    parser.add_argument("--region", help="Capture region: x,y,w,h")
    parser.add_argument("--monitor", type=int, default=1, help="Monitor number")
    args = parser.parse_args()

    # Find the ESP32
    if args.host:
        ws_url = f"ws://{args.host}:{args.port}/stream"
    else:
        ws_url = discover_esp32()
        if not ws_url:
            sys.exit("Could not find espGIF on the network.\n"
                     "Make sure the ESP32 is on and connected to the same WiFi.\n"
                     "Or use --host <ip> to specify manually.")

    # Pick converter
    convert = try_numpy_convert()

    # Connect
    print(f"Connecting to {ws_url}...")
    ws = websocket.WebSocket()
    ws.settimeout(5)
    try:
        ws.connect(ws_url)
    except Exception as e:
        sys.exit(f"Connection failed: {e}")
    print("Connected! Streaming...")

    frame_interval = 1.0 / args.fps
    region = parse_region(args.region) if args.region else None
    frames_sent = 0
    start_time = time.time()

    try:
        with mss.mss() as sct:
            monitor = sct.monitors[args.monitor]
            capture_area = region or monitor

            while True:
                t0 = time.time()

                # Capture screen
                shot = sct.grab(capture_area)
                img = Image.frombytes("RGB", shot.size, shot.rgb)

                # Convert to RGB565
                data = convert(img)

                # Send
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

                # Stats every 5 seconds
                total_elapsed = time.time() - start_time
                if frames_sent % (args.fps * 5) == 0:
                    actual_fps = frames_sent / total_elapsed
                    print(f"  {frames_sent} frames | {actual_fps:.1f} FPS | "
                          f"{elapsed*1000:.0f}ms/frame")

    except KeyboardInterrupt:
        print(f"\nStopped. Sent {frames_sent} frames.")
    finally:
        ws.close()


if __name__ == "__main__":
    main()
