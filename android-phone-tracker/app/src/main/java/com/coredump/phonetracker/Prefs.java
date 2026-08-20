package com.coredump.phonetracker;

import android.content.Context;
import android.content.SharedPreferences;

/** Everything the service needs to know, in one place. */
public class Prefs {
    private static final String FILE = "phonetracker";

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

    public String host()  { return p.getString(HOST, ""); }
    public int    port()  { return p.getInt(PORT, 9000); }

    /**
     * The server files everything under an imei and pads it to sixteen digits, so a phone needs
     * one too. It is just an identifier here - any sixteen digits that no watch is using.
     */
    public String imei()  { return p.getString(IMEI, ""); }

    /** seconds between position reports */
    public int    interval() { return p.getInt(INTERVAL, 600); }
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

    public boolean configured() {
        return host().length() > 0 && imei().length() > 0;
    }
}
