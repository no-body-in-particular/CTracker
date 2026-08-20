package com.coredump.phonetracker;

import android.content.Context;
import android.location.Location;
import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.util.Locale;

/**
 * What the phone will do when the server tells it to.
 *
 * A word on the shell command. The tracker protocol has no authentication at all: a connection is
 * identified by an imei it states itself, in the clear, over plain tcp. Anything able to reach
 * the port can therefore claim to be this phone and be sent whatever the command file holds. A
 * general shell on that footing is a remote root hole on the phone, not a feature, so it is off
 * unless deliberately switched on and, even then, every command has to carry a token set on the
 * device. The rest of the commands are harmless enough to leave open: the worst an intruder gets
 * is a phone that rings.
 */
public class DeviceCommands {

    public interface Host {
        void reportNow();
        void setInterval(int seconds);
        String status();
        void send(String message);
    }

    private static final String TAG = "PhoneTracker";

    private final Context context;
    private final Prefs prefs;
    private final Warnings warnings;
    private final Host host;

    public DeviceCommands(Context context, Prefs prefs, Warnings warnings, Host host) {
        this.context = context;
        this.prefs = prefs;
        this.warnings = warnings;
        this.host = host;
    }

    public void handle(String raw) {
        Log.i(TAG, "command: " + raw);

        String command = raw.trim();
        //the web interface sends the watch commands with a trailing '#', so accept them that way
        if (command.endsWith("#")) {
            command = command.substring(0, command.length() - 1);
        }

        String upper = command.toUpperCase(Locale.US);
        String argument = argumentOf(command);

        if (upper.startsWith("WARNAUDIO") || upper.startsWith("WARNMOTOR")) {
            warnings.warnAudible(argument);
            host.send(BasicProtocol.commandResult("alerting: " + argument));
            return;
        }

        if (upper.startsWith("WARN")) {
            warnings.warn(argument);
            host.send(BasicProtocol.commandResult("warned: " + argument));
            return;
        }

        if (upper.startsWith("QUIET") || upper.startsWith("STOPALARM")) {
            warnings.stopSound();
            host.send(BasicProtocol.commandResult("alarm stopped"));
            return;
        }

        if (upper.startsWith("LOCATE") || upper.startsWith("WHERE")) {
            host.reportNow();
            host.send(BasicProtocol.commandResult("reporting position"));
            return;
        }

        if (upper.startsWith("INTERVAL") || upper.startsWith("UPDATE=")) {
            int seconds = 0;

            try {
                seconds = Integer.parseInt(argument.replaceAll("[^0-9]", ""));
            } catch (Exception ignored) { }

            if (seconds >= 30 && seconds <= 86400) {
                host.setInterval(seconds);
                host.send(BasicProtocol.commandResult("interval now " + seconds + "s"));
            } else {
                host.send(BasicProtocol.commandResult("interval must be 30..86400 seconds"));
            }

            return;
        }

        if (upper.startsWith("STATUS")) {
            host.send(BasicProtocol.commandResult(host.status()));
            return;
        }

        if (upper.startsWith("SHELL")) {
            host.send(BasicProtocol.commandResult(shell(argument)));
            return;
        }

        host.send(BasicProtocol.commandResult("unknown command: " + command));
    }

    /** everything after the first ';' or space, which is where the argument starts */
    private String argumentOf(String command) {
        int cut = command.indexOf(';');

        if (cut < 0) {
            cut = command.indexOf(' ');
        }

        return cut < 0 ? "" : command.substring(cut + 1).trim();
    }

    private String shell(String argument) {
        if (!prefs.shellEnabled()) {
            return "shell is disabled on this device";
        }

        String token = prefs.shellToken();

        if (token.length() < 8) {
            return "shell needs a token of at least 8 characters set on the device";
        }

        //SHELL;<token>;<command>
        int cut = argument.indexOf(';');

        if (cut < 0 || !argument.substring(0, cut).equals(token)) {
            return "shell token missing or wrong";
        }

        String line = argument.substring(cut + 1);

        if (line.trim().isEmpty()) {
            return "nothing to run";
        }

        return run(line);
    }

    private String run(String line) {
        Process p = null;

        try {
            //su if it is there, plain sh if it is not - this is useful either way
            try {
                p = Runtime.getRuntime().exec("su");
            } catch (Exception noRoot) {
                p = Runtime.getRuntime().exec("sh");
            }

            OutputStreamWriter w = new OutputStreamWriter(p.getOutputStream());
            w.write(line + "\nexit\n");
            w.flush();

            StringBuilder out = new StringBuilder();
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
            String l;

            //a command that prints a lot must not be allowed to fill the socket buffer
            while ((l = r.readLine()) != null && out.length() < 4000) {
                out.append(l).append('\n');
            }

            BufferedReader e = new BufferedReader(new InputStreamReader(p.getErrorStream()));

            while ((l = e.readLine()) != null && out.length() < 4000) {
                out.append(l).append('\n');
            }

            p.waitFor();
            return out.length() == 0 ? "(no output)" : out.toString().trim();

        } catch (Exception ex) {
            return "failed: " + ex.getMessage();

        } finally {
            if (p != null) {
                p.destroy();
            }
        }
    }

    public static String describe(Location fix) {
        if (fix == null) {
            return "no fix";
        }

        return String.format(Locale.US, "%.6f,%.6f +-%.0fm", fix.getLatitude(), fix.getLongitude(),
                fix.getAccuracy());
    }
}
