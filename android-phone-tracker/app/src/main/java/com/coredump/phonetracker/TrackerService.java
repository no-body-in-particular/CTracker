package com.coredump.phonetracker;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.location.Location;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.PowerManager;
import android.util.Log;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * The tracker itself: a foreground service that reports where the phone is, on an interval, for
 * as long as it is switched on.
 *
 * It has to be a foreground service with a visible notification. Anything else is killed within
 * minutes of the screen going off, and a tracker that stops tracking when you put the phone in
 * your pocket is worse than none - you would trust a gap in the history to mean you were not
 * moving.
 */
public class TrackerService extends Service implements ServerLink.CommandListener, DeviceCommands.Host {

    private static final String TAG = "PhoneTracker";
    private static final String CHANNEL_RUNNING = "phonetracker_running";
    private static final int NOTIFICATION_ID = 4710;
    public static final String ACTION_STOP = "com.coredump.phonetracker.STOP";

    private Prefs prefs;
    private DeviceState state;
    private ServerLink link;
    private Warnings warnings;
    private DeviceCommands commands;

    private HandlerThread worker;
    private Handler handler;
    private PowerManager.WakeLock wakeLock;

    private volatile boolean running;
    private long lastReportAt;
    private String lastResult = "not reported yet";
    private Float lastSteps;

    @Override public IBinder onBind(Intent intent) { return null; }

    @Override public void onCreate() {
        super.onCreate();
        prefs = new Prefs(this);
        state = new DeviceState(this);
        warnings = new Warnings(this);
        commands = new DeviceCommands(this, prefs, warnings, this);

        worker = new HandlerThread("tracker");
        worker.start();
        handler = new Handler(worker.getLooper());

        PowerManager power = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wakeLock = power.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "phonetracker:report");
        wakeLock.setReferenceCounted(false);
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_STOP.equals(intent.getAction())) {
            stopTracking();
            return START_NOT_STICKY;
        }

        if (!prefs.configured()) {
            Log.w(TAG, "not configured, refusing to start");
            stopSelf();
            return START_NOT_STICKY;
        }

        startForeground(NOTIFICATION_ID, notification("starting"));

        if (!running) {
            running = true;
            prefs.set(Prefs.RUNNING, true);
            state.start();
            link = new ServerLink(prefs.host(), prefs.port(), this);
            handler.post(reportCycle);
        }

        //START_STICKY so that a phone which kills the process under memory pressure brings the
        //tracker back rather than quietly stopping recording
        return START_STICKY;
    }

    private final Runnable reportCycle = new Runnable() {
        @Override public void run() {
            if (!running) {
                return;
            }

            try {
                wakeLock.acquire(60000);
                report();
            } catch (Exception e) {
                Log.w(TAG, "report failed: " + e.getMessage());
            } finally {
                try { wakeLock.release(); } catch (Exception ignored) { }
            }

            if (running) {
                handler.postDelayed(this, Math.max(30, prefs.interval()) * 1000L);
            }
        }
    };

    private void report() {
        Location fix = state.location();
        List<String> macs = state.wifiMacs(12);
        int battery = state.battery();

        String message = BasicProtocol.position(prefs.imei(), battery,
                fix == null ? null : fix.getLatitude(),
                fix == null ? null : fix.getLongitude(),
                macs);

        boolean sent = link.send(message);
        lastReportAt = System.currentTimeMillis();
        lastResult = sent
                ? (fix == null ? "sent, no fix (" + macs.size() + " networks)" : "sent " + DeviceCommands.describe(fix))
                : "server unreachable";

        if (sent) {
            sendReadings();
        }

        updateNotification();
    }

    /**
     * Readings that are not a position. The stock server has no field for these in the basic
     * protocol, so they go as their own message - a server without the handler logs them and
     * carries on, which is why they are sent separately rather than bolted onto the position.
     */
    private void sendReadings() {
        Float bpm = state.heartRate();

        if (bpm != null) {
            link.send(BasicProtocol.stat(prefs.imei(), "heartrate", bpm));
        }

        Float steps = state.steps();

        //the step counter is a total since boot, so it only means something as a difference, and
        //the server records it in thousands the same way the watch's is recorded
        if (steps != null && (lastSteps == null || steps > lastSteps)) {
            link.send(BasicProtocol.stat(prefs.imei(), "steps_k", steps / 1000f));
            lastSteps = steps;
        }
    }

    @Override public void onCommand(String command) {
        commands.handle(command);
    }

    @Override public void reportNow() {
        handler.post(new Runnable() {
            @Override public void run() {
                try {
                    wakeLock.acquire(30000);
                    report();
                } finally {
                    try { wakeLock.release(); } catch (Exception ignored) { }
                }
            }
        });
    }

    @Override public void setInterval(int seconds) {
        prefs.set(Prefs.INTERVAL, seconds);
        handler.removeCallbacks(reportCycle);
        handler.postDelayed(reportCycle, seconds * 1000L);
        updateNotification();
    }

    @Override public String status() {
        Location fix = state.location();
        Float bpm = state.heartRate();

        return "interval " + prefs.interval() + "s"
                + ", battery " + state.battery() + "%"
                + ", " + DeviceCommands.describe(fix)
                + (bpm == null ? "" : ", " + Math.round(bpm) + " bpm")
                + ", last report " + lastResult;
    }

    @Override public void send(String message) {
        link.send(message);
    }

    private void stopTracking() {
        running = false;
        prefs.set(Prefs.RUNNING, false);
        handler.removeCallbacks(reportCycle);
        state.stop();

        if (link != null) {
            link.close();
        }

        stopForeground(true);
        stopSelf();
    }

    @Override public void onDestroy() {
        running = false;
        state.stop();

        if (link != null) {
            link.close();
        }

        if (worker != null) {
            worker.quitSafely();
        }

        super.onDestroy();
    }

    private void updateNotification() {
        try {
            NotificationManager m = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
            m.notify(NOTIFICATION_ID, notification(lastResult));
        } catch (Exception ignored) { }
    }

    private Notification notification(String text) {
        NotificationManager m = (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel c = new NotificationChannel(CHANNEL_RUNNING, "Tracking",
                    NotificationManager.IMPORTANCE_LOW);       //no sound for the standing notice
            c.setShowBadge(false);
            m.createNotificationChannel(c);
        }

        Intent open = new Intent(this, MainActivity.class);
        PendingIntent tap = PendingIntent.getActivity(this, 0, open,
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0);

        Notification.Builder b = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_RUNNING)
                : new Notification.Builder(this);

        String when = lastReportAt == 0 ? ""
                : " at " + new SimpleDateFormat("HH:mm", Locale.US).format(new Date(lastReportAt));

        b.setContentTitle("Tracking to " + prefs.host())
         .setContentText(text + when)
         .setSmallIcon(android.R.drawable.ic_menu_mylocation)
         .setOngoing(true)
         .setContentIntent(tap);

        return b.build();
    }
}
