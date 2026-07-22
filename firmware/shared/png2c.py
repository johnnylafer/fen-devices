#!/usr/bin/env python3
"""PNG -> RGB565 C array for Arduino_GFX draw16bitRGBBitmap()."""
import subprocess, sys, pathlib

def convert(png, name, out):
    # raw RGB via ImageMagick (no PIL dependency)
    r = subprocess.run(["magick", png, "-depth", "8", "rgb:-"], capture_output=True, check=True)
    raw = r.stdout
    ident = subprocess.run(["magick", "identify", "-format", "%w %h", png], capture_output=True, check=True)
    w, h = map(int, ident.stdout.split())
    assert len(raw) == w * h * 3, f"{png}: raw size mismatch"
    px = []
    for i in range(0, len(raw), 3):
        rr, gg, bb = raw[i], raw[i+1], raw[i+2]
        px.append(((rr & 0xF8) << 8) | ((gg & 0xFC) << 3) | (bb >> 3))
    with open(out, "w") as f:
        f.write(f"// generated from {pathlib.Path(png).name} — {w}x{h} RGB565\n")
        f.write("#include <stdint.h>\n#include <pgmspace.h>\n")
        f.write(f"#define {name.upper()}_W {w}\n#define {name.upper()}_H {h}\n")
        f.write(f"const uint16_t {name}[] PROGMEM = {{\n")
        for i in range(0, len(px), 16):
            f.write("  " + ",".join(f"0x{v:04X}" for v in px[i:i+16]) + ",\n")
        f.write("};\n")
    print(f"{out}: {w}x{h} ({len(px)*2//1024} KB)")

if __name__ == "__main__":
    base = pathlib.Path(__file__).parent
    convert(base/"fen-220.png", "fen_art", base/"img_fen_220.h")
    convert(base/"fen-110.png", "fen_art_sm", base/"img_fen_110.h")
    convert(base/"qr-300.png", "qr_img", base/"img_qr_300.h")
    convert(base/"qr-130.png", "qr_img_sm", base/"img_qr_130.h")
