# Commands the server accepts

Anything typed into the command box, or sent by the buttons beside it, is written to
`/var/gps/<imei>.command.txt` and picked up by whichever connection currently owns the
device. What happens next depends on the protocol that device speaks - a command one
protocol implements is not necessarily implemented by another, and `COMMAND_FUNCTION`
forwards anything it does not recognise to the device verbatim.

**End every command with `#`.** Most of the older ones take their terminating character from
whatever you typed, so `UPDATE=600#` works and `UPDATE=600` goes out unterminated: the device
waits for an end of packet that never comes and silently discards it. The commands added more
recently append the `#` themselves whichever way it was written.

## Thinkrace / IW (the PT880 and similar watches)

Everything here has been sent to a watch and answered.

| Command | What it does |
|---|---|
| `LOCATE#` | fix now |
| `RESTART#` | reboot |
| `SHUTDOWN#` | power off |
| `FACTORYALL#` | factory reset |
| `SYNCTIME#` | set the clock, sent automatically on connect |
| `HEARTRATE#` | one heart rate reading |
| `SPO2#` | one blood oxygen reading |
| `TEMP#` | one temperature reading |
| `PHOTO#` | take a picture; it arrives over the next minute or so |
| `RECORD#` | record ten seconds from the microphone and upload it |
| `UPDATE=<seconds>#` | how often to report a position |
| `HEALTHINT=<hr>,<bp>#` | how often the watch reports health, in minutes, on its own |
| `MODE=<n>#` | working mode |
| `TIMES=<hhmm@hhmm>#` | working hours |
| `HOURS=12#` / `HOURS=24#` | clock format |
| `PHONE=0#` / `PHONE=1#` | allow or block calls |
| `MOTION=0#` / `MOTION=1#` | motion detection |
| `MSG=<text>#` | show a message on the watch |
| `SMS=<command>#` | run one of the watch's own `#...#` commands - see below |

### The SMS tunnel

`SMS=` carries one of the watch's internal commands over the data connection, so none of
them need an actual text message. Write the command as you would text it; the escaping the
protocol needs is done for you.

    SMS=#status#            SMS=#deviceinfo#        SMS=#capture#
    SMS=#reboot#            SMS=#poweroff#          SMS=#location#
    SMS=#USB#=adb           SMS=#listen#<number>    SMS=#getWeather#

`#listen#<number>` makes the watch **telephone** that number and open its microphone - it is
a call, not a recording. `RECORD#` is the one that produces a file.

The full vocabulary, and where it came from, is in the `pt880-root` repository under
`docs/protocol-commands.md`.

## Myrope R18 and similar watches

| Command | What it does |
|---|---|
| `LOCATE#` `SHUTDOWN#` `REBOOT#` `FACTORYALL#` | as above |
| `FIND#` | make the watch sound so it can be found |
| `MONITOR#` | call back and listen |
| `HEARTRATE#` `TEMPERATURE#` | one reading |
| `EMERGENCY#` | raise an alarm |
| `UPLOAD=<seconds>` | reporting interval |
| `SOS=` `SOSSMS=` `OWNER=` `CENTER=` | numbers the watch calls or texts |
| `LOWBAT=` `OFFAL=` | low battery and removal alarm settings |
| `REMOVE=` `REMOVESMS=` | removal alarm |
| `MSG=<text>` | show a message |

## XEXUN, Megastek, Myrope, JIMI

| Protocol | Commands |
|---|---|
| XEXUN | `UPDATE=<seconds>` |
| Megastek | `UPDATE=<seconds>` |
| Myrope | `UPDATE=<seconds>` `MSG=<text>` `ALM=` |
| JIMI | `STATUS#`, plus anything the device itself accepts, forwarded as typed |

## Pictures and recordings

A completed picture or recording is stored in `/var/gps/<imei>.images.db` or
`<imei>.audio.db`, written as one header line and one line of hex so the file stays readable:

    CTIMG2 <unix ts> <device time> <bytes> <lat> <lon>
    ffd8ffe000104a4649...

Both appear on the map, in the event list and next to the command that asked for them.
Recordings are converted for playback in the browser and the original AMR is still offered
beside the player. One can be lifted out with nothing but shell:

    grep -A1 '^CTIMG2 1787743075' 0355932600098953.images.db | tail -1 | xxd -r -p > out.jpg

or with `device_server/tools/extract-images.py`, which reads either store and names each
file by what it turns out to be.

## Health readings

Where a device can be told its own reporting period it is told once, rather than asked every
few minutes - `HEALTHINT` does that, and `DEVICE_HEALTH_INTERVAL_MIN` in `config.h` sets what
the server asks for. Readings then arrive on their own. Set it to 0 to go back to polling.

A device that stops sending readings altogether is restarted, once, after
`HEALTH_RECOVERY_TIMEOUT`. That window is deliberately much longer than the reporting period:
a watch that has been taken off for a while, or that dropped a few uploads, is not a watch
that needs rebooting.
