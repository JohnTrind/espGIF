"""
Convert all GIFs in a folder to 172x320, rotated 90° clockwise.
Usage: python convert_gifs.py [input_folder] [output_folder]
Defaults: input=./gifs_raw  output=./gifs_ready
"""

import sys
import os
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Installing Pillow...")
    os.system(f"{sys.executable} -m pip install Pillow")
    from PIL import Image


def convert_gif(src: Path, dst: Path, target_w=172, target_h=320):
    img = Image.open(src)
    frames = []
    durations = []
    n_frames = getattr(img, "n_frames", 1)

    for i in range(n_frames):
        img.seek(i)
        frame = img.convert("RGBA")

        # Rotate 90° clockwise
        frame = frame.rotate(-90, expand=True)

        # Scale to fit target, keeping aspect ratio
        fw, fh = frame.size
        scale = min(target_w / fw, target_h / fh)
        new_w = int(fw * scale)
        new_h = int(fh * scale)
        frame = frame.resize((new_w, new_h), Image.LANCZOS)

        # Center on black canvas
        canvas = Image.new("RGBA", (target_w, target_h), (0, 0, 0, 255))
        ox = (target_w - new_w) // 2
        oy = (target_h - new_h) // 2
        canvas.paste(frame, (ox, oy), frame)

        # Convert to P mode for GIF
        frames.append(canvas.convert("RGB"))
        durations.append(img.info.get("duration", 100))

    if not frames:
        return

    frames[0].save(
        dst,
        save_all=True,
        append_images=frames[1:],
        duration=durations,
        loop=img.info.get("loop", 0),
        optimize=False,
    )
    print(f"  {src.name} -> {dst.name}  ({n_frames} frames, {target_w}x{target_h})")


def main():
    in_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("gifs_raw")
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("gifs_ready")

    if not in_dir.exists():
        in_dir.mkdir(parents=True)
        print(f"Created input folder: {in_dir}")
        print("Put your GIF files in there and run again.")
        return

    out_dir.mkdir(parents=True, exist_ok=True)

    gifs = list(in_dir.glob("*.gif")) + list(in_dir.glob("*.GIF"))
    if not gifs:
        print(f"No GIFs found in {in_dir}")
        return

    print(f"Converting {len(gifs)} GIFs to 172x320 + 90° rotation...")
    for gif in sorted(gifs):
        dst = out_dir / gif.name
        try:
            convert_gif(gif, dst)
        except Exception as e:
            print(f"  {gif.name} FAILED: {e}")

    print(f"\nDone! Output in: {out_dir}")


if __name__ == "__main__":
    main()
