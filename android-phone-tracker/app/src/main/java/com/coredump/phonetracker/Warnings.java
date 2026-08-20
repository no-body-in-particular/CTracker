package com.coredump.phonetracker;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.media.AudioManager;
import android.media.Ringtone;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;

/**
 * Alerting the person holding the phone. The server raises these from a geofence being crossed or
 * from a command typed into the web interface, and until now the basic protocol had nowhere to
 * send them - basic_warn and basic_warn_audio were empty.
 *
 * Three strengths: a notification, a vibration, and a sound loud enough to find a phone with.
 */
public class Warnings {

    public static final String CHANNEL_ALERT = "phonetracker_alert";
    private static final int ALERT_ID = 4711;
    private static final long AUDIBLE_MS = 20000;

    private final Context context;
    private final NotificationManager notifications;
    private Ringtone ringing;

    public Warnings(Context context) {
        this.context = context.getApplicationContext();
        notifications = (NotificationManager) this.context.getSystemService(Context.NOTIFICATION_SERVICE);
        createChannel();
    }

    private void createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel c = new NotificationChannel(CHANNEL_ALERT, "Tracker alerts",
                    NotificationManager.IMPORTANCE_HIGH);
            c.setDescription("Warnings raised by the tracker server");
            c.enableVibration(true);
            notifications.createNotificationChannel(c);
        }
    }

    /** A notification and a vibration - enough to notice, not enough to embarrass. */
    public void warn(String reason) {
        notify(reason);
        vibrate(new long[]{0, 400, 200, 400, 200, 400});
    }

    /**
     * The same, plus the alarm tone at full volume for twenty seconds. This is the one for
     * finding a phone that has been put down somewhere, so it deliberately ignores silent mode by
     * using the alarm stream.
     */
    public void warnAudible(String reason) {
        notify(reason);
        vibrate(new long[]{0, 800, 300, 800, 300, 800, 300, 800});

        try {
            stopSound();

            AudioManager audio = (AudioManager) context.getSystemService(Context.AUDIO_SERVICE);

            if (audio != null) {
                audio.setStreamVolume(AudioManager.STREAM_ALARM,
                        audio.getStreamMaxVolume(AudioManager.STREAM_ALARM), 0);
            }

            Uri tone = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM);

            if (tone == null) {
                tone = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE);
            }

            ringing = RingtoneManager.getRingtone(context, tone);

            if (ringing != null) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    ringing.setAudioAttributes(new android.media.AudioAttributes.Builder()
                            .setUsage(android.media.AudioAttributes.USAGE_ALARM)
                            .setContentType(android.media.AudioAttributes.CONTENT_TYPE_SONIFICATION)
                            .build());
                }

                ringing.play();

                //it has to stop on its own: nobody is going to be holding the phone to silence it
                new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
                    @Override public void run() { stopSound(); }
                }, AUDIBLE_MS);
            }

        } catch (Exception ignored) { }
    }

    public void stopSound() {
        try {
            if (ringing != null && ringing.isPlaying()) {
                ringing.stop();
            }
        } catch (Exception ignored) { }

        ringing = null;
    }

    private void vibrate(long[] pattern) {
        try {
            Vibrator v = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);

            if (v == null || !v.hasVibrator()) {
                return;
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                v.vibrate(VibrationEffect.createWaveform(pattern, -1));
            } else {
                v.vibrate(pattern, -1);
            }

        } catch (Exception ignored) { }
    }

    private void notify(String reason) {
        try {
            Notification.Builder b = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                    ? new Notification.Builder(context, CHANNEL_ALERT)
                    : new Notification.Builder(context);

            b.setContentTitle("Tracker alert")
             .setContentText(reason == null || reason.isEmpty() ? "Warning from the server" : reason)
             .setSmallIcon(android.R.drawable.ic_dialog_alert)
             .setAutoCancel(true);

            notifications.notify(ALERT_ID, b.build());

        } catch (Exception ignored) { }
    }
}
