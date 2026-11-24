"""
espGIF Installer — Flash firmware and configure WiFi.
Auto-detects ESP32-S3 boards, lets you pick a COM port, configure WiFi, and flash.

Usage: python install.py
"""

import sys
import os
import time
import subprocess
from pathlib import Path

# Fix Windows console encoding
if sys.platform == "win32":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    os.system("")  # enable ANSI escape codes on Windows

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("Installing pyserial...")
    os.system(f"{sys.executable} -m pip install pyserial")
    import serial
    import serial.tools.list_ports

# --- Paths ---

SCRIPT_DIR = Path(__file__).parent
PROJECT_DIR = SCRIPT_DIR.parent
BUILD_DIR = PROJECT_DIR / ".pio" / "build" / "esp32s3"
BOOTLOADER = BUILD_DIR / "bootloader.bin"
PARTITIONS = BUILD_DIR / "partitions.bin"
FIRMWARE = BUILD_DIR / "firmware.bin"
BOOT_APP0 = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"
ESPTOOL = Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool.py"

# --- TUI helpers ---

CYAN = "\033[96m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
BOLD = "\033[1m"
DIM = "\033[2m"
RESET = "\033[0m"


def banner():
    print(f"""
{CYAN}{BOLD}  +===================================+
  |         espGIF Installer           |
  +===================================+{RESET}
""")


def step(num, text):
    print(f"\n{CYAN}{BOLD}[{num}]{RESET} {BOLD}{text}{RESET}")


def ok(text):
    print(f"  {GREEN}✓{RESET} {text}")


def warn(text):
    print(f"  {YELLOW}!{RESET} {text}")


def err(text):
    print(f"  {RED}✗{RESET} {text}")


def prompt(text, default=""):
    suffix = f" [{default}]" if default else ""
    result = input(f"  {text}{suffix}: ").strip()
    return result if result else default


# --- ESP32 detection ---

ESP32_S3_VID_PID = "303A:1001"


def find_esp32_ports():
    """Find all ESP32-S3 USB serial ports."""
    ports = []
    for p in serial.tools.list_ports.comports():
        hwid = p.hwid or ""
        if ESP32_S3_VID_PID.upper() in hwid.upper():
            ports.append(p)
    return ports


def select_port():
    """Auto-detect or let user pick a COM port."""
    esp_ports = find_esp32_ports()
    all_ports = list(serial.tools.list_ports.comports())

    if len(esp_ports) == 1:
        p = esp_ports[0]
        ok(f"Auto-detected: {BOLD}{p.device}{RESET} ({p.serial_number or 'ESP32-S3'})")
        use = prompt("Use this port? (Y/n)", "Y")
        if use.lower() != "n":
            return p.device

    if esp_ports:
        print(f"  Found {len(esp_ports)} ESP32-S3 board(s):\n")
    else:
        warn("No ESP32-S3 detected. Showing all COM ports:\n")
        esp_ports = all_ports

    for i, p in enumerate(esp_ports):
        sn = p.serial_number or ""
        desc = p.description or ""
        tag = f" {DIM}SN:{sn}{RESET}" if sn else ""
        print(f"    {BOLD}{i + 1}.{RESET} {p.device}  {DIM}{desc}{RESET}{tag}")

    print()
    while True:
        choice = prompt(f"Select port (1-{len(esp_ports)})", "1")
        try:
            idx = int(choice) - 1
            if 0 <= idx < len(esp_ports):
                return esp_ports[idx].device
        except ValueError:
            pass
        err("Invalid choice")


# --- WiFi config ---

def get_wifi_config():
    """Ask for WiFi credentials."""
    print()
    ssid = prompt("WiFi SSID (leave empty to skip)")
    if not ssid:
        return None, None
    password = prompt("WiFi password")
    return ssid, password


# --- Flash ---

def check_firmware():
    """Check that firmware binaries exist."""
    missing = []
    for f in [BOOTLOADER, PARTITIONS, FIRMWARE]:
        if not f.exists():
            missing.append(f.name)
    if not BOOT_APP0.exists():
        missing.append("boot_app0.bin")
    if not ESPTOOL.exists():
        missing.append("esptool.py")

    if missing:
        err(f"Missing files: {', '.join(missing)}")
        warn("Run 'pio run' in the project directory first to build the firmware.")
        return False
    return True


def flash_firmware(port):
    """Flash all firmware binaries to the ESP32."""
    cmd = [
        sys.executable, str(ESPTOOL),
        "--chip", "esp32s3",
        "--port", port,
        "--baud", "921600",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0000", str(BOOTLOADER),
        "0x8000", str(PARTITIONS),
        "0xe000", str(BOOT_APP0),
        "0x10000", str(FIRMWARE),
    ]

    print(f"\n  {DIM}$ esptool.py --port {port} write_flash ...{RESET}\n")

    proc = subprocess.run(cmd, capture_output=False)
    return proc.returncode == 0


# --- Serial WiFi config ---

def send_wifi_config(port, ssid, password):
    """Send WiFi credentials via serial after boot."""
    print(f"\n  Waiting for device to boot...", end="", flush=True)

    # Wait a moment for the device to reboot after flashing
    time.sleep(2)

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        err(f"Cannot open {port}: {e}")
        return False

    # Wait for READY signal (up to 10 seconds)
    start = time.time()
    ready = False
    while time.time() - start < 10:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if line:
            if "READY" in line:
                ready = True
                break
            print(f"\n  {DIM}  {line}{RESET}", end="", flush=True)
        print(".", end="", flush=True)

    if not ready:
        print()
        warn("Didn't receive READY signal — device may need manual reset.")
        warn("You can configure WiFi via the web UI (AP mode: espGIF-Setup)")
        ser.close()
        return False

    print(f" {GREEN}ready!{RESET}")

    # Send WiFi credentials
    cmd = f"WIFI:{ssid}:{password}\n"
    ser.write(cmd.encode())
    ser.flush()

    # Wait for confirmation
    start = time.time()
    while time.time() - start < 3:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if line.startswith("OK:WIFI:"):
            ok(f"WiFi configured: {BOLD}{ssid}{RESET}")
            ser.close()
            return True

    ser.close()
    warn("No confirmation received — WiFi may not be saved")
    return False


def wait_for_ip(port):
    """Wait and show the device's IP address."""
    print(f"\n  Waiting for WiFi connection...", end="", flush=True)

    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except:
        return None

    start = time.time()
    ip = None
    while time.time() - start < 45:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if "Connected! IP:" in line:
            ip = line.split("IP:")[1].strip().split()[0]
            print(f" {GREEN}{ip}{RESET}")
            break
        elif "AP Mode" in line:
            print(f" {YELLOW}AP Mode{RESET}")
            ok("Connect to WiFi 'espGIF-Setup' and open http://192.168.4.1")
            break
        elif line:
            pass  # suppress boot noise
        print(".", end="", flush=True)

    ser.close()
    return ip


# --- Main ---

def main():
    banner()

    # Step 1: Check firmware
    step(1, "Checking firmware")
    if not check_firmware():
        return

    ok(f"Bootloader: {BOOTLOADER.name}")
    ok(f"Partitions: {PARTITIONS.name}")
    ok(f"Firmware:   {FIRMWARE.name} ({FIRMWARE.stat().st_size / 1024:.0f} KB)")

    # Step 2: Select port
    step(2, "Detecting ESP32-S3")
    port = select_port()
    if not port:
        err("No port selected")
        return

    # Step 3: WiFi config
    step(3, "WiFi configuration")
    ssid, password = get_wifi_config()
    if ssid:
        ok(f"Will configure: {BOLD}{ssid}{RESET}")
    else:
        warn("Skipped — device will start in AP mode (espGIF-Setup)")

    # Confirm
    print()
    go = prompt(f"Flash {BOLD}{port}{RESET} now? (Y/n)", "Y")
    if go.lower() == "n":
        print(f"\n{YELLOW}Cancelled.{RESET}")
        return

    # Step 4: Flash
    step(4, "Flashing firmware")
    if not flash_firmware(port):
        err("Flashing failed!")
        return
    ok("Firmware flashed successfully!")

    # Step 5: Configure WiFi via serial
    if ssid:
        step(5, "Configuring WiFi")
        send_wifi_config(port, ssid, password)

        # Step 6: Wait for connection
        step(6, "Connecting to network")
        ip = wait_for_ip(port)
        if ip:
            ok(f"Web UI: {BOLD}http://{ip}{RESET}")
            ok(f"mDNS:   {BOLD}http://espgif.local{RESET}")
    else:
        step(5, "Done!")
        ok(f"Device is in AP mode")
        ok(f"Connect to WiFi: {BOLD}espGIF-Setup{RESET}")
        ok(f"Then open: {BOLD}http://192.168.4.1{RESET}")

    print(f"""
{GREEN}{BOLD}  +===================================+
  |       Installation complete!       |
  +===================================+{RESET}
""")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n{YELLOW}Cancelled.{RESET}")
