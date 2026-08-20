package com.coredump.phonetracker;

import java.util.List;
import java.util.Locale;

/**
 * The server's "basic" protocol, the one it already recognises as a phone.
 *
 *   BASIC;<imei>;<battery>;<lat,lon>;<mac|mac|mac>!
 *
 * Fields are separated by ';' and a message ends with '!'. The server identifies the connection
 * by looking for "BASIC;" in the first seven bytes, so the first message sent has to be one of
 * these - a command result cannot come first, and carries no imei of its own.
 *
 * Coming back the other way, commands are newline terminated plain strings.
 */
public class BasicProtocol {

    public static final char TERMINATOR = '!';
    private static final char SEPARATOR = ';';

    /**
     * A ';' inside a value would be read as a field break. The server already swaps byte 255
     * back to ';' when it reads a command result, so that is the convention both ends use.
     */
    public static String escape(String value) {
        if (value == null) {
            return "";
        }

        return value.replace(';', (char) 255).replace('!', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    /** BASIC;imei;battery;lat,lon;wifi! - the position report. */
    public static String position(String imei, int battery, Double lat, Double lon, List<String> macs) {
        StringBuilder b = new StringBuilder();
        b.append("BASIC").append(SEPARATOR)
         .append(escape(imei)).append(SEPARATOR)
         .append(Math.max(0, Math.min(100, battery))).append(SEPARATOR);

        //the server treats a field under five characters as "no fix given" and falls back to
        //placing the phone by its wifi scan, which is what should happen when there is no fix
        if (lat != null && lon != null) {
            b.append(String.format(Locale.US, "%.6f,%.6f", lat, lon));
        }

        b.append(SEPARATOR);

        if (macs != null) {
            for (int i = 0; i < macs.size(); i++) {
                if (i > 0) {
                    b.append('|');
                }
                b.append(macs.get(i));
            }
        }

        return b.append(TERMINATOR).toString();
    }

    /**
     * STAT;imei;name;value! - a reading that is not a position. The stock server had nowhere to
     * put a pulse or a step count from a phone, so this is a small addition on that side; a
     * server without it logs the message and carries on.
     */
    public static String stat(String imei, String name, double value) {
        return "STAT" + SEPARATOR + escape(imei) + SEPARATOR + escape(name) + SEPARATOR
                + String.format(Locale.US, "%.2f", value) + TERMINATOR;
    }

    /** CMDRESULT;output! - what a command produced. */
    public static String commandResult(String output) {
        return "CMDRESULT" + SEPARATOR + escape(output) + TERMINATOR;
    }
}
