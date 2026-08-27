#!/usr/bin/env bash
# Repair device log files whose lines have been spliced into each other.
#
#     ./repair-logs.sh [--apply] [dir]
#
# Several connection threads share one log file and write it without a lock, so
# a line occasionally lands in the middle of another - "sent command: I" then a
# whole different entry, then "WBPXL". It is rare, about one line in two
# thousand, but it breaks anything that parses the log by line and it hides the
# entries it swallows.
#
# What this can and cannot do. Where a timestamp appears mid-line the two
# entries are simply split apart, and both survive. Where the characters of two
# entries are interleaved rather than concatenated, they cannot be separated -
# the information to do it is gone - so those fragments are moved to a
# .corrupt sidecar rather than thrown away.
#
# Reads only, unless --apply is given. The original is kept as .prerepair.
set -u
APPLY=0
[ "${1:-}" = "--apply" ] && { APPLY=1; shift; }
DIR="${1:-/var/gps}"

TS='[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z'

total_bad=0
for f in "$DIR"/*.log.txt; do
  [ -f "$f" ] || continue
  bad=$(sudo grep -acvE "^$TS," "$f" 2>/dev/null; true)
  bad=${bad:-0}
  [ "$bad" -eq 0 ] && continue
  lines=$(sudo wc -l < "$f")
  printf '%-44s %6s bad of %7s lines\n' "$(basename "$f")" "$bad" "$lines"
  total_bad=$((total_bad + bad))

  [ "$APPLY" -eq 1 ] || continue

  sudo cp -a "$f" "$f.prerepair"
  sudo python3 - "$f" <<'PY'
import re, sys, os
path = sys.argv[1]
TS = re.compile(r'(?=\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z,)')
GOOD = re.compile(r'^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z,')
kept, orphans = [], []
with open(path, 'r', errors='replace') as fh:
    for line in fh:
        line = line.rstrip('\n')
        if GOOD.match(line) and not TS.search(line[1:]):
            kept.append(line)
            continue
        # split wherever another entry begins inside this one
        for piece in TS.split(line):
            if not piece:
                continue
            (kept if GOOD.match(piece) else orphans).append(piece)
kept.sort(key=lambda l: l[:20])
with open(path + '.new', 'w') as fh:
    fh.write('\n'.join(kept) + '\n')
if orphans:
    with open(path + '.corrupt', 'a') as fh:
        fh.write('\n'.join(orphans) + '\n')
os.replace(path + '.new', path)
print(f"    repaired: {len(kept)} entries kept, {len(orphans)} fragments set aside")
PY
  sudo chown --reference="$f.prerepair" "$f" 2>/dev/null
  sudo chmod --reference="$f.prerepair" "$f" 2>/dev/null
done

echo
if [ "$APPLY" -eq 1 ]; then
  echo "repaired. originals kept as .prerepair, unrecoverable fragments in .corrupt"
else
  echo "$total_bad damaged lines across the logs. Re-run with --apply to repair."
fi
