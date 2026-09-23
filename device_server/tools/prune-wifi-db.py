#!/usr/bin/env python3
"""Drop wifi.db entries that contradict what every other entry says about the same
access points.

The server learns where access points are from devices' own GPS fixes. Before
wifi_learn_position() gained its guards, it learned from moving devices too - and a scan
reported alongside a fix taken at speed describes wherever the device was when the frame
went out, not where the scan happened. One drive at 41 km/h on 2026-09-20 wrote six entries
putting a set of home access points 3.2 km away, and every wifi-only lookup afterwards
alternated between the real position and that one.

The guards stop new entries like that being written. This removes the ones already there.

The rule is the same one wifi_consensus() applies at runtime, turned on the database itself:
for each entry, ask what every *other* entry sharing an access point with it says, and if
the answer is well supported and this entry sits further than DISAGREE_M from it, this entry
is the odd one out. Nothing is removed on its own say-so - an entry whose access points
nobody else has ever reported is simply the only thing known about them and is kept.

    ./prune-wifi-db.py /var/gps/wifi.db            # report only
    ./prune-wifi-db.py /var/gps/wifi.db --apply    # rewrite it

Stop the daemon before applying. It holds the whole database in memory and writes it back
every CACHE_SAVE_TIME seconds, so a file edited underneath a running server is overwritten
with the copy that still has the bad entries in it.

The record layout is device_server/wifi_lookup.h: sixteen fixed wifi_network slots, a count,
then a location_result. All packed, all little endian.
"""
import math
import os
import struct
import sys
from collections import defaultdict

NETWORK_SLOTS = 16
NETWORK_SIZE = 9          # 6 byte mac, 2 pad, 1 rssi
ENTRY_SIZE = 173          # 144 networks + 8 count + 21 location_result
COUNT_OFF = NETWORK_SLOTS * NETWORK_SIZE
RESULT_OFF = COUNT_OFF + 8

# matches WIFI_CONSENSUS_RADIUS and WIFI_LEARN_MAX_DISAGREE in config.h
CLUSTER_M = 300.0
DISAGREE_M = 500.0
MIN_VOTES = 2

# Only entries the server wrote itself are removed by default. WIFI_LEARN_RADIUS marked them
# as 10 m before it started telling the truth about what it knows, and that is the one value
# nothing else produces: the geolocation services report their own accuracy, which comes back
# as anything from 19 to a few hundred. Those are a different kind of claim - an outside
# service saying where it thinks the access points are - and if one of those disagrees it is
# at least as likely that the local consensus is the wrong half. Pass --all to include them.
SELF_LEARNED_RADIUS = 10

# An entry is only removed when the position it contradicts is properly supported. Two votes
# can themselves be a pair of bad entries agreeing with each other, and evicting a good entry
# on that basis would be the same fault in the other direction.
EVICT_MIN_VOTES = 5


def normalise(mac):
    """The same collapse wifi_lookup.c does: a block of consecutive BSSIDs is one radio,
    and a locally administered address is derived from a real one."""
    return bytes([mac[0] & ~0x02]) + mac[1:5] + bytes([mac[5] & ~0x07])


def metres(a_lat, a_lon, b_lat, b_lon):
    r = 6371000.0
    p1, p2 = math.radians(a_lat), math.radians(b_lat)
    dp = math.radians(b_lat - a_lat)
    dl = math.radians(b_lon - a_lon)
    x = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * r * math.asin(math.sqrt(x))


def read_db(path):
    blob = open(path, 'rb').read()
    claimed = struct.unpack_from('<Q', blob, 0)[0]
    fits = (len(blob) - 8) // ENTRY_SIZE
    if claimed > fits:
        print(f"  file claims {claimed} entries but only {fits} fit; reading {fits}")
        claimed = fits
    entries = []
    for i in range(claimed):
        rec = blob[8 + i * ENTRY_SIZE: 8 + (i + 1) * ENTRY_SIZE]
        count = min(struct.unpack_from('<Q', rec, COUNT_OFF)[0], NETWORK_SLOTS)
        lat, lng, radius, valid, tried = struct.unpack_from('<fffBQ', rec, RESULT_OFF)
        macs = {normalise(rec[j * NETWORK_SIZE: j * NETWORK_SIZE + 6]) for j in range(count)}
        entries.append({'raw': rec, 'macs': macs, 'lat': lat, 'lng': lng,
                        'radius': radius, 'valid': bool(valid)})
    return entries


def anchor_of(mac, entries, by_mac, skip):
    """Where an access point actually lives: the heaviest cluster of positions it has been
    reported at, ignoring one entry. Returns None when nothing else has seen it - an access
    point known only from the entry under test is no evidence against that entry."""
    seen = [(entries[o]['lat'], entries[o]['lng']) for o in by_mac.get(mac, ())
            if o != skip and entries[o]['valid']]
    if not seen:
        return None

    cell = {}
    for la, lo in seen:
        key = (round(la / 0.0027), round(lo / 0.0044))
        cell[key] = cell.get(key, 0) + 1

    best_key = max(cell, key=lambda k: sum(cell.get((k[0] + dy, k[1] + dx), 0)
                                           for dy in (-1, 0, 1) for dx in (-1, 0, 1)))
    near = [(la, lo) for la, lo in seen
            if abs(round(la / 0.0027) - best_key[0]) <= 1
            and abs(round(lo / 0.0044) - best_key[1]) <= 1]
    return (sum(la for la, _ in near) / len(near),
            sum(lo for _, lo in near) / len(near),
            len(near))


def verdict(idx, entries, by_mac):
    """Is this entry a scan filed in the wrong place?

    Asking whether the entry as a whole disagrees with its neighbours is not enough, and
    gets the wrong answer on real data. A phone hotspot travels with its owner: one was
    reported in 249 entries spread over two cities. Any entry containing it shares an access
    point with every place that owner has been, so an honest record of a genuinely new place
    looks like it contradicts the biggest cluster - and deleting it would throw away the only
    thing known about that place.

    What separates the two is whether the entry has any access point of its own. A real scan
    of somewhere new is mostly access points that live there. A scan filed in the wrong place
    - the device moved, the scan did not - is entirely access points that live somewhere
    else, because that is where it was taken.

    So an entry is only condemned when every one of its access points is anchored elsewhere
    and at least one of them is anchored firmly.
    """
    e = entries[idx]
    local = 0
    strongest_elsewhere = 0
    for mac in e['macs']:
        a = anchor_of(mac, entries, by_mac, idx)
        if a is None:
            local += 1          # nobody else has seen it, so it belongs wherever this says
            continue
        if metres(e['lat'], e['lng'], a[0], a[1]) <= DISAGREE_M:
            local += 1
        elif a[2] > strongest_elsewhere:
            strongest_elsewhere = a[2]

    if local == 0 and strongest_elsewhere >= EVICT_MIN_VOTES:
        return strongest_elsewhere
    return 0


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    apply_it = '--apply' in sys.argv
    every = '--all' in sys.argv

    entries = read_db(path)
    print(f"  {len(entries)} entries")

    by_mac = defaultdict(list)
    for i, e in enumerate(entries):
        for mac in e['macs']:
            by_mac[mac].append(i)

    doomed = []
    spared = []
    for i, e in enumerate(entries):
        if not e['valid']:
            continue
        support = verdict(i, entries, by_mac)
        if not support:
            continue
        if not every and round(e['radius']) != SELF_LEARNED_RADIUS:
            spared.append(i)
            continue
        doomed.append((i, support, None))

    print(f"  {len(doomed)} self-learned entries hold nothing but access points that live elsewhere")
    if spared:
        print(f"  {len(spared)} more disagree but came from a geolocation service - left alone, pass --all to include them")
    print()
    for i, support, _ in sorted(doomed, key=lambda d: -d[1])[:20]:
        e = entries[i]
        print(f"    {e['lat']:.6f},{e['lng']:.6f} r={e['radius']:.0f}"
              f"  all {len(e['macs'])} of its access points live elsewhere"
              f"  ({support} sightings anchor the firmest of them)")
    if len(doomed) > 20:
        print(f"    ... and {len(doomed) - 20} more")

    if not apply_it:
        print("\n  report only - pass --apply to rewrite the file")
        return 0
    if not doomed:
        print("\n  nothing to remove")
        return 0

    drop = {i for i, _, _ in doomed}
    kept = [e['raw'] for i, e in enumerate(entries) if i not in drop]
    tmp = path + '.pruned'
    with open(tmp, 'wb') as fh:
        fh.write(struct.pack('<Q', len(kept)))
        for rec in kept:
            fh.write(rec)
    os.replace(tmp, path)
    print(f"\n  rewrote {path}: {len(entries)} -> {len(kept)} entries")
    return 0


if __name__ == '__main__':
    sys.exit(main())
