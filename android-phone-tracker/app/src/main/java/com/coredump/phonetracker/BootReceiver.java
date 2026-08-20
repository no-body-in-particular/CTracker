package com.coredump.phonetracker;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

/** A tracker that has to be started by hand after every reboot will have gaps nobody notices. */
public class BootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        Prefs prefs = new Prefs(context);

        //Start on every boot, as long as there is something usable to start with - which, with
        //the built in defaults, there always is. The point of a phone tracker is that it does not
        //depend on someone remembering to launch it; a reboot in a pocket should not leave a gap
        //in the history. It records that it is meant to be running so the service, and its own
        //restart-on-kill, agree with what booted it.
        if (!prefs.configured()) {
            return;
        }

        prefs.set(Prefs.RUNNING, true);

        Intent start = new Intent(context, TrackerService.class);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            context.startForegroundService(start);
        } else {
            context.startService(start);
        }
    }
}
