#!/usr/bin/env python3
"""Pull the pictures back out of an <imei>.images.db.

The container is deliberately simple enough that this script is a convenience rather than a
requirement. Each record is one ASCII header line followed by exactly that many bytes of
image and a newline:

    CTIMG1 <unix_ts> <device_time> <bytes> <lat> <lon>\\n
    <bytes>
    \\n

so `strings file.images.db | grep CTIMG1` already lists what is in there, and any single
picture can be lifted out with dd once you have the offset this script prints.

  list    : extract-images.py file.images.db
  extract : extract-images.py file.images.db outdir/
"""
import os, sys

# CTIMG2/CTIMG1 are pictures, CTAUD1 a monitor recording. Same container, different payload.
MAGICS = (b"CTIMG2 ", b"CTIMG1 ", b"CTAUD1 ")
RAW_MAGICS = (b"CTIMG1 ",)


def _next_magic(blob, pos):
    """Offset of the earliest record header at or after pos, and which magic it was."""
    best, best_magic = -1, None
    for m in MAGICS:
        i = blob.find(m, pos)
        if i >= 0 and (best < 0 or i < best):
            best, best_magic = i, m
    return best, best_magic


def records(blob):
    """Yield (offset, header_fields, data) for every record found."""
    pos = 0
    while True:
        start, magic = _next_magic(blob, pos)
        if start < 0:
            return
        eol = blob.find(b"\n", start)
        if eol < 0:
            return
        parts = blob[start:eol].split()
        if len(parts) < 4:
            pos = eol + 1
            continue
        try:
            length = int(parts[3])
        except ValueError:
            pos = eol + 1
            continue

        # length is the size of the picture; a hex payload occupies twice that
        onwire = length if magic in RAW_MAGICS else length * 2
        raw = blob[eol + 1: eol + 1 + onwire]
        if len(raw) != onwire:
            sys.stderr.write(f"warning: record at {start} is truncated "
                             f"({len(raw)} of {onwire} bytes on disk)\n")

        if magic not in RAW_MAGICS:
            try:
                data = bytes.fromhex(raw.decode("ascii", "strict"))
            except (ValueError, UnicodeDecodeError):
                sys.stderr.write(f"warning: record at {start} is not valid hex, skipping\n")
                pos = eol + 1 + onwire
                continue
        else:
            data = raw

        yield eol + 1, parts, data
        pos = eol + 1 + onwire


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else None
    blob = open(path, "rb").read()

    n = 0
    for offset, parts, data in records(blob):
        n += 1
        ts, devtime, length = parts[1].decode(), parts[2].decode(), parts[3].decode()
        lat = parts[4].decode() if len(parts) > 4 else "?"
        lon = parts[5].decode() if len(parts) > 5 else "?"
        kind = ("jpeg" if data[:2] == b"\xff\xd8"
                else "amr" if data[:5] == b"#!AMR" else "data")
        print(f"{n:4d}  ts={ts}  taken={devtime}  {length:>8} bytes  "
              f"offset={offset}  {lat},{lon}  {kind}")
        if outdir:
            os.makedirs(outdir, exist_ok=True)
            ext = "jpg" if kind == "jpeg" else "amr" if kind == "amr" else "bin"
            name = os.path.join(outdir, f"{ts}_{devtime}.{ext}")
            with open(name, "wb") as fh:
                fh.write(data)
            print(f"      -> {name}")

    if n == 0:
        print("no records found", file=sys.stderr)
        return 1
    if not outdir:
        print(f"\n{n} image(s). Pass an output directory to write them out, or by hand:")
        print(f"  grep -A1 '^CTIMG2 <ts>' {path} | tail -1 | xxd -r -p > out.jpg")
        print(f"  (a recording is the same, with CTAUD1 and .amr)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
