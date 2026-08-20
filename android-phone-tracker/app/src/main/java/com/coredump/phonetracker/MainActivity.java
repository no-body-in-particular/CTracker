package com.coredump.phonetracker;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

/** Setup and a start/stop switch. Everything that matters happens in the service. */
public class MainActivity extends Activity {

    private Prefs prefs;
    private EditText host, port, imei, interval, token;
    private CheckBox shell;
    private TextView state;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        setContentView(R.layout.activity_main);
        prefs = new Prefs(this);

        host = findViewById(R.id.host);
        port = findViewById(R.id.port);
        imei = findViewById(R.id.imei);
        interval = findViewById(R.id.interval);
        token = findViewById(R.id.token);
        shell = findViewById(R.id.shell);
        state = findViewById(R.id.state);

        //the getters return the defaults when nothing is stored yet, so writing what they return
        //straight back persists the defaults on first launch - after this they are saved settings
        //like any other, not values that only exist while the getter supplies them
        save();

        host.setText(prefs.host());
        port.setText(String.valueOf(prefs.port()));
        imei.setText(prefs.imei());
        interval.setText(String.valueOf(prefs.interval()));
        token.setText(prefs.shellToken());
        shell.setChecked(prefs.shellEnabled());

        ((Button) findViewById(R.id.start)).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { save(); start(); }
        });

        ((Button) findViewById(R.id.stop)).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { stop(); }
        });

        askForPermissions();
        showState();
    }

    private void askForPermissions() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return;
        }

        //Background location is a separate grant from Android 10 and has to be asked for after
        //the foreground one, not alongside it - asked together, both are refused.
        requestPermissions(new String[]{
                Manifest.permission.ACCESS_FINE_LOCATION,
                Manifest.permission.ACCESS_COARSE_LOCATION,
                Manifest.permission.BODY_SENSORS,
                Manifest.permission.ACTIVITY_RECOGNITION
        }, 1);
    }

    @Override public void onRequestPermissionsResult(int code, String[] perms, int[] granted) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q && code == 1) {
            requestPermissions(new String[]{Manifest.permission.ACCESS_BACKGROUND_LOCATION}, 2);
        }
    }

    private void save() {
        //the fields carry the defaults until the user edits them, so an empty one means "use the
        //default", not "clear it" - falling back to the getter keeps a blank field from wiping a
        //good value
        prefs.set(Prefs.HOST, orDefault(host, prefs.host()));
        prefs.set(Prefs.PORT, number(port == null ? "" : port.getText().toString(), prefs.port()));
        prefs.set(Prefs.IMEI, orDefault(imei, prefs.imei()));
        prefs.set(Prefs.INTERVAL, Math.max(30, number(interval == null ? "" : interval.getText().toString(), prefs.interval())));
        prefs.set(Prefs.SHELL_TOKEN, token == null ? prefs.shellToken() : token.getText().toString().trim());
        prefs.set(Prefs.SHELL_ENABLED, shell == null ? prefs.shellEnabled() : shell.isChecked());
    }

    private String orDefault(android.widget.EditText field, String fallback) {
        if (field == null) {
            return fallback;
        }

        String value = field.getText().toString().trim();
        return value.isEmpty() ? fallback : value;
    }

    private int number(String text, int fallback) {
        try {
            return Integer.parseInt(text.trim());
        } catch (Exception e) {
            return fallback;
        }
    }

    private void start() {
        if (!prefs.configured()) {
            Toast.makeText(this, "A server address and an id are needed", Toast.LENGTH_LONG).show();
            return;
        }

        Intent i = new Intent(this, TrackerService.class);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            startForegroundService(i);
        } else {
            startService(i);
        }

        showState();
    }

    private void stop() {
        Intent i = new Intent(this, TrackerService.class);
        i.setAction(TrackerService.ACTION_STOP);
        startService(i);
        prefs.set(Prefs.RUNNING, false);
        showState();
    }

    private void showState() {
        state.setText(prefs.running() ? "Tracking" : "Stopped");
    }
}
