package com.coredump.phonetracker;

import android.content.Context;
import android.content.SharedPreferences;

/** Everything the service needs to know, in one place. */
public class Prefs {
    private static final String FILE = "phonetracker";

    //Sensible starting points so a fresh install is one tap from working: the interval the watch
    //settles on, the server this talks to, and the identifier the layout was only suggesting -
    //which is now the real default rather than a hint the field started empty behind.
    public static final String DEFAULT_HOST = "coredump.ws";
    public static final int    DEFAULT_PORT = 9000;
    public static final String DEFAULT_IMEI = "0000000000000001";
    public static final int    DEFAULT_INTERVAL = 180;

    public static final String HOST = "host";
    public static final String PORT = "port";
    public static final String IMEI = "imei";
    public static final String INTERVAL = "interval";
    public static final String RUNNING = "running";
    public static final String SHELL_ENABLED = "shell_enabled";
    public static final String SHELL_TOKEN = "shell_token";

    private final SharedPreferences p;

    public Prefs(Context c) {
        p = c.getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    public String host()  { return p.getString(HOST, DEFAULT_HOST); }
    public int    port()  { return p.getInt(PORT, DEFAULT_PORT); }

    /**
     * The server files everything under an imei and pads it to sixteen digits, so a phone needs
     * one too. It is just an identifier here - any sixteen digits that no watch is using.
     */
    public String imei()  { return p.getString(IMEI, DEFAULT_IMEI); }

    /** seconds between position reports */
    public int    interval() { return p.getInt(INTERVAL, DEFAULT_INTERVAL); }
    public boolean running() { return p.getBoolean(RUNNING, false); }

    /**
     * Running arbitrary commands is off unless it is deliberately switched on, and even then it
     * needs the token below. The tracker protocol has no authentication of any kind - anything
     * that can reach the port and knows the imei can send commands - so without this a shell on
     * a rooted phone would be handed to whoever found the port.
     */
    public boolean shellEnabled() { return p.getBoolean(SHELL_ENABLED, false); }
    public String  shellToken()   { return p.getString(SHELL_TOKEN, ""); }

    public void set(String key, String value) { p.edit().putString(key, value).apply(); }
    public void set(String key, int value)    { p.edit().putInt(key, value).apply(); }
    public void set(String key, boolean value){ p.edit().putBoolean(key, value).apply(); }

    /**
     * There are working defaults for all of these now, so "configured" cannot just mean they are
     * non-empty - it always would be. It means they are actually usable: a host, a port in range,
     * and sixteen digits of identifier.
     */
    public boolean configured() {
        return host().length() > 0
                && port() > 0 && port() < 65536
                && imei().replaceAll("[^0-9]", "").length() >= 10;
    }
}
