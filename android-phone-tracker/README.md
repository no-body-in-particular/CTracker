# Phone Tracker

A small Android app that reports this phone to the tracker server, using the server's existing
"basic" protocol (`device_server/device/basic_protocol.c`) - the one it already identifies as a
phone. Meant as a personal fallback for when the watch is not to hand: where the phone has been,
plus whatever sensors it happens to carry.

## What it sends

    BASIC;<imei>;<battery>;<lat,lon>;<mac|mac|mac>!   position + wifi scan
    STAT;<imei>;heartrate;<bpm>!                      a reading that is not a position
    STAT;<imei>;steps_k;<thousands>!
    CMDRESULT;<output>!                               the result of a command

No fix is sent as an empty coordinate field, and the server places the phone by its wifi scan,
exactly as it does for the watch. The `imei` is any sixteen digits no watch is using - it is only
the key the server files data under.

## What it accepts, over the same connection

    WARN / WARNAUDIO / WARNMOTOR    notify, vibrate, and (audio) sound the alarm at full volume
    QUIET                           stop the alarm
    LOCATE                          report a position now
    INTERVAL;<seconds>              change how often it reports
    STATUS                          report what it is doing
    SHELL;<token>;<command>         run a command (off by default - see below)

The geofences in the web interface work against this phone the same as against the watch: a
crossing raises WARN, and the alarm button raises WARNAUDIO.

## The shell command

The tracker protocol has no authentication: a connection states its own imei, in the clear, over
plain tcp, and anything that can reach the port can claim to be this phone. A general shell on
that footing is a remote root hole, so it is off unless switched on in the app and, even then,
every command must carry a token set on the device. Leave it off unless you have a specific need
and understand that footing.

## Building

    ./gradlew :app:assembleDebug

Needs an Android SDK with platform 30 and build-tools 30.x. Output is
`app/build/outputs/apk/debug/app-debug.apk`. Install on a phone with USB debugging on, or push it
to a rooted phone directly. Grant location (including "all the time"), and body-sensors if the
phone has a heart rate sensor.

To survive reboots it re-registers a foreground service; some vendor ROMs additionally need the
app exempted from battery optimisation or it is killed within minutes of the screen going off.
