package com.coredump.phonetracker;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Bundle;
import android.os.Looper;
import android.util.Log;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;

/**
 * What the phone can say about itself: where it is, what it can see, and what its sensors read.
 *
 * Location comes from LocationManager rather than the fused provider on purpose - this has to run
 * on a rooted or degoogled rom where play services may not exist. Both providers are listened to
 * and the better recent fix wins.
 */
public class DeviceState implements LocationListener, SensorEventListener {

    private static final String TAG = "PhoneTracker";
    //a fix older than this describes where the phone was, not where it is
    private static final long FIX_MAX_AGE_MS = 5 * 60 * 1000L;

    private final Context context;
    private final LocationManager locations;
    private final WifiManager wifi;
    private final SensorManager sensors;

    private Location best;
    private Float heartRate;
    private long heartRateAt;
    private Float steps;

    public DeviceState(Context context) {
        this.context = context.getApplicationContext();
        locations = (LocationManager) this.context.getSystemService(Context.LOCATION_SERVICE);
        wifi = (WifiManager) this.context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
        sensors = (SensorManager) this.context.getSystemService(Context.SENSOR_SERVICE);
    }

    public boolean hasLocationPermission() {
        return context.checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION)
                == PackageManager.PERMISSION_GRANTED;
    }

    public void start() {
        if (hasLocationPermission()) {
            request(LocationManager.GPS_PROVIDER);
            request(LocationManager.NETWORK_PROVIDER);
        }

        listen(Sensor.TYPE_HEART_RATE);
        listen(Sensor.TYPE_STEP_COUNTER);
    }

    private void request(String provider) {
        try {
            if (locations.isProviderEnabled(provider)) {
                locations.requestLocationUpdates(provider, 30000, 10, this, Looper.getMainLooper());
            }
        } catch (Exception e) {
            Log.w(TAG, "cannot use " + provider + ": " + e.getMessage());
        }
    }

    private void listen(int type) {
        Sensor s = sensors == null ? null : sensors.getDefaultSensor(type);

        //most phones have a step counter and almost none have a heart rate sensor. absence is
        //normal, not a fault - the reading is simply never reported.
        if (s != null) {
            sensors.registerListener(this, s, SensorManager.SENSOR_DELAY_NORMAL);
        }
    }

    public void stop() {
        try { locations.removeUpdates(this); } catch (Exception ignored) { }
        try { sensors.unregisterListener(this); } catch (Exception ignored) { }
    }

    @Override public void onLocationChanged(Location location) {
        if (location == null) {
            return;
        }

        //a newer fix wins, and so does a much more accurate one of similar age
        if (best == null
                || location.getTime() > best.getTime() + 30000
                || location.getAccuracy() < best.getAccuracy()) {
            best = location;
        }
    }

    @Override public void onStatusChanged(String provider, int status, Bundle extras) { }
    @Override public void onProviderEnabled(String provider) { }
    @Override public void onProviderDisabled(String provider) { }

    @Override public void onSensorChanged(SensorEvent event) {
        if (event.sensor.getType() == Sensor.TYPE_HEART_RATE) {
            if (event.values[0] > 0) {
                heartRate = event.values[0];
                heartRateAt = System.currentTimeMillis();
            }

        } else if (event.sensor.getType() == Sensor.TYPE_STEP_COUNTER) {
            steps = event.values[0];
        }
    }

    @Override public void onAccuracyChanged(Sensor sensor, int accuracy) { }

    /** The last usable fix, or null when there is nothing recent enough to be worth sending. */
    public Location location() {
        Location fix = best;

        if (fix == null && hasLocationPermission()) {
            try {
                fix = locations.getLastKnownLocation(LocationManager.GPS_PROVIDER);

                if (fix == null) {
                    fix = locations.getLastKnownLocation(LocationManager.NETWORK_PROVIDER);
                }
            } catch (Exception ignored) { }
        }

        if (fix != null && System.currentTimeMillis() - fix.getTime() > FIX_MAX_AGE_MS) {
            return null;
        }

        return fix;
    }

    public Float heartRate() {
        //a pulse from an hour ago is not this moment's pulse
        if (heartRate != null && System.currentTimeMillis() - heartRateAt < 30 * 60 * 1000L) {
            return heartRate;
        }

        return null;
    }

    public Float steps() { return steps; }

    public int battery() {
        try {
            Intent status = context.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));

            if (status != null) {
                int level = status.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
                int scale = status.getIntExtra(BatteryManager.EXTRA_SCALE, -1);

                if (level >= 0 && scale > 0) {
                    return Math.round(level * 100f / scale);
                }
            }
        } catch (Exception ignored) { }

        return 0;
    }

    /**
     * The access points in range, strongest first. The server places a phone by these when there
     * is no fix, and files them against the fix when there is - which is what builds the wifi
     * database it uses later.
     */
    public List<String> wifiMacs(int limit) {
        List<String> macs = new ArrayList<>();

        if (wifi == null || !hasLocationPermission()) {
            return macs;          //scan results are location data, and are withheld without it
        }

        try {
            wifi.startScan();
            List<ScanResult> results = wifi.getScanResults();

            if (results == null) {
                return macs;
            }

            Collections.sort(results, new Comparator<ScanResult>() {
                @Override public int compare(ScanResult a, ScanResult b) {
                    return Integer.compare(b.level, a.level);
                }
            });

            for (ScanResult r : results) {
                if (r.BSSID == null) {
                    continue;
                }

                String mac = r.BSSID.toLowerCase();

                //00:00:00:00:00:00 is never a real access point, and the server drops it anyway
                if (mac.equals("00:00:00:00:00:00") || mac.equals("ff:ff:ff:ff:ff:ff")) {
                    continue;
                }

                macs.add(mac);

                if (macs.size() >= limit) {
                    break;
                }
            }

        } catch (Exception e) {
            Log.w(TAG, "wifi scan unavailable: " + e.getMessage());
        }

        return macs;
    }
}
