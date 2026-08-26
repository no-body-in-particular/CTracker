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

MAGIC = b"CTIMG1 "


def records(blob):
    """Yield (offset, header_fields, data) for every record found."""
    pos = 0
    while True:
        start = blob.find(MAGIC, pos)
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
        data = blob[eol + 1: eol + 1 + length]
        if len(data) != length:
            sys.stderr.write(f"warning: record at {start} is truncated "
                             f"({len(data)} of {length} bytes)\n")
        yield eol + 1, parts, data
        pos = eol + 1 + length


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
        kind = "jpeg" if data[:2] == b"\xff\xd8" else "data"
        print(f"{n:4d}  ts={ts}  taken={devtime}  {length:>8} bytes  "
              f"offset={offset}  {lat},{lon}  {kind}")
        if outdir:
            os.makedirs(outdir, exist_ok=True)
            name = os.path.join(outdir, f"{ts}_{devtime}.jpg")
            with open(name, "wb") as fh:
                fh.write(data)
            print(f"      -> {name}")

    if n == 0:
        print("no records found", file=sys.stderr)
        return 1
    if not outdir:
        print(f"\n{n} image(s). Pass an output directory to write them out, or lift one by hand:")
        print(f"  dd if={path} bs=1 skip=<offset> count=<bytes> of=out.jpg")
    return 0


if __name__ == "__main__":
    sys.exit(main())
