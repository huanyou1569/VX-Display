"""
merge_slices.py  -- 合并多个单帧 .slices 文件为多帧动画 .slices。

用法:
    python merge_slices.py frame_*.slices -o anim.slices

支持: format=0x02 (v1), format=0x03 (v2), format=0x04 (v3 6-bit).

VXSL header (16 bytes):
    0: magic "VXSL" (4B)
    4: strips  (2B)  = 32
    6: leds    (2B)  = 24
    8: depth   (2B)  = 0 (static) / slice_count (anim)
   10: frames  (2B)  = slice_count (static) / N (anim frames)
   12: format  (1B)

   format=0x02:
   13-15: reserved
   No palette section.

   format=0x03:
   13: palette_r, 14: palette_g, 15: palette_b
   No separate palette section.

   format=0x04:
   13: reserved
   14-15: palette_count (2B, typically 64)
   16-207: palette data (64 x 3 bytes R,G,B)
   Frame data starts at offset 208.
"""

import struct
import sys
import argparse
import glob
import os

HEADER_FMT = "<4sHHHHBBBB"
HEADER_SIZE = 16
SLICE_STRIPS = 32
SLICE_LEDS = 24
SLICE_SIZE = SLICE_STRIPS * SLICE_LEDS * 3    # 2304 (state-packed)
BIT_SIZE = SLICE_STRIPS * SLICE_LEDS // 8       # 96 (1-bit)
V3_VOXELS = SLICE_STRIPS * SLICE_LEDS          # 768
V3_SLICE_BYTES = (V3_VOXELS * 6 + 7) // 8       # 576 (6-bit packed)


def read_header(path):
    with open(path, "rb") as f:
        data = f.read(HEADER_SIZE)
        if len(data) < HEADER_SIZE:
            raise ValueError(f"{path}: too small ({len(data)} bytes)")
        magic, strips, leds, _depth, _frames, fmt, r0, r1, r2 = \
            struct.unpack(HEADER_FMT, data)
        if magic != b"VXSL":
            raise ValueError(f"{path}: not VXSL")

        slice_count = _depth if _depth > 0 else _frames

        hdr = {
            "strips": strips,
            "leds": leds,
            "slice_count": slice_count,
            "format": fmt,
            "path": path,
        }
        if fmt == 0x03:
            hdr["palette_r"] = r0
            hdr["palette_g"] = r1
            hdr["palette_b"] = r2
        elif fmt == 0x04:
            palette_count = struct.unpack_from('<H', data, 14)[0]
            pal_data = f.read(palette_count * 3)
            if len(pal_data) != palette_count * 3:
                raise ValueError(f"{path}: truncated palette")
            hdr["palette_count"] = palette_count
            hdr["palette_data"] = pal_data
        return hdr


def main():
    parser = argparse.ArgumentParser(description="Merge .slices files to multi-frame animation")
    parser.add_argument("inputs", nargs="+")
    parser.add_argument("-o", "--output", required=True)
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()

    files = []
    for pat in args.inputs:
        matches = glob.glob(pat) if "*" in pat or "?" in pat else [pat]
        files.extend(matches)
    files = sorted(set(f for f in files if os.path.isfile(f)))

    if len(files) < 2:
        print(f"Error: need >= 2 files (found {len(files)})")
        sys.exit(1)

    headers = []
    for fp in files:
        try:
            headers.append(read_header(fp))
        except ValueError as e:
            print(f"Skip {fp}: {e}")

    if len(headers) < 2:
        print("Error: < 2 valid input files")
        sys.exit(1)

    ref = headers[0]
    is_v3 = (ref["format"] == 0x04)
    is_v2 = (ref["format"] == 0x03)

    for h in headers[1:]:
        if h["slice_count"] != ref["slice_count"]:
            raise ValueError(f"{h['path']}: slice count mismatch")
        if h["format"] != ref["format"]:
            raise ValueError(f"{h['path']}: format mismatch")
        if is_v2:
            if (h["palette_r"] != ref["palette_r"] or
                h["palette_g"] != ref["palette_g"] or
                h["palette_b"] != ref["palette_b"]):
                raise ValueError(f"{h['path']}: palette color mismatch")
        if is_v3:
            if h["palette_data"] != ref["palette_data"]:
                raise ValueError(f"{h['path']}: palette data mismatch")

    total_frames = len(headers)
    slice_count = ref["slice_count"]

    if is_v3:
        frame_size = slice_count * V3_SLICE_BYTES   # 50 * 576 = 28,800
        fmt_name = "0x04 (6-bit 64-color)"
    elif is_v2:
        frame_size = slice_count * BIT_SIZE          # 50 * 96 = 4,800
        fmt_name = "0x03 (1-bit + palette)"
    else:
        frame_size = slice_count * SLICE_SIZE        # 100 * 2304 = 225KB
        fmt_name = "0x02 (state-packed)"

    if args.verbose:
        print(f"Input: {total_frames} frames, format={fmt_name}, "
              f"per frame: {slice_count} slices x {frame_size // slice_count}B = {frame_size}B ({frame_size/1024:.1f}KB)")

    with open(args.output, "wb") as out:
        # Write header
        if is_v3:
            header = struct.pack(HEADER_FMT,
                b"VXSL", ref["strips"], ref["leds"],
                slice_count, total_frames,
                0x04, 0, 0, 0)
            # Fix bytes 14-15: palette_count (little-endian)
            header = bytearray(header)
            pc = ref["palette_count"]
            header[14] = pc & 0xFF
            header[15] = (pc >> 8) & 0xFF
            out.write(bytes(header))
            out.write(ref["palette_data"])
        elif is_v2:
            header = struct.pack(HEADER_FMT,
                b"VXSL", ref["strips"], ref["leds"],
                slice_count, total_frames,
                0x03, ref["palette_r"], ref["palette_g"], ref["palette_b"])
            out.write(header)
        else:
            header = struct.pack(HEADER_FMT,
                b"VXSL", ref["strips"], ref["leds"],
                slice_count, total_frames,
                ref["format"], 0, 0, 0)
            out.write(header)

        # Copy frame data
        for i, h in enumerate(headers):
            with open(h["path"], "rb") as inf:
                if is_v3:
                    inf.seek(HEADER_SIZE + ref["palette_count"] * 3)
                else:
                    inf.seek(HEADER_SIZE)
                data = inf.read(frame_size)
                if len(data) != frame_size:
                    raise ValueError(f"{h['path']}: expected {frame_size}B, got {len(data)}B")
                out.write(data)
            if args.verbose:
                print(f"  [{i+1}/{total_frames}] {os.path.basename(h['path'])}")

    actual = os.path.getsize(args.output)
    palette_size = ref.get("palette_count", 0) * 3 if is_v3 else 0
    expected = HEADER_SIZE + palette_size + total_frames * frame_size
    print(f"Output: {args.output}")
    print(f"  Frames: {total_frames}, slices: {slice_count}, format: {fmt_name}")
    print(f"  Size: {actual/1024:.1f} KB ({'OK' if actual == expected else f'expected {expected/1024:.1f} KB'})")


if __name__ == "__main__":
    main()
