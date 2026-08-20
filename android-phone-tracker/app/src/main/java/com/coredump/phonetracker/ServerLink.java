package com.coredump.phonetracker;

import android.util.Log;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * The socket to the tracker server, and the reading of commands off it.
 *
 * The connection is expected to drop - a phone moves between cells and wifi all day - so nothing
 * here treats that as an error worth reporting. It reconnects on the next send, and the caller
 * keeps queueing regardless.
 */
public class ServerLink {

    public interface CommandListener {
        void onCommand(String command);
    }

    private static final String TAG = "PhoneTracker";
    private static final int CONNECT_TIMEOUT_MS = 15000;
    //the server drops a basic connection after 1200s of silence, so anything under that keeps it
    private static final int READ_TIMEOUT_MS = 60000;

    private final String host;
    private final int port;
    private final CommandListener listener;

    private Socket socket;
    private OutputStream out;
    private InputStream in;
    private Thread reader;
    private final StringBuilder pending = new StringBuilder();

    public ServerLink(String host, int port, CommandListener listener) {
        this.host = host;
        this.port = port;
        this.listener = listener;
    }

    public synchronized boolean connected() {
        return socket != null && socket.isConnected() && !socket.isClosed();
    }

    private synchronized void connect() throws Exception {
        if (connected()) {
            return;
        }

        close();
        socket = new Socket();
        socket.connect(new InetSocketAddress(host, port), CONNECT_TIMEOUT_MS);
        socket.setSoTimeout(READ_TIMEOUT_MS);
        socket.setKeepAlive(true);
        out = socket.getOutputStream();
        in = socket.getInputStream();
        startReader();
        Log.i(TAG, "connected to " + host + ":" + port);
    }

    /** Sends a message, connecting first if it has to. Returns false if it could not. */
    public boolean send(String message) {
        try {
            connect();

            synchronized (this) {
                out.write(message.getBytes("UTF-8"));
                out.flush();
            }

            return true;

        } catch (Exception e) {
            Log.w(TAG, "send failed: " + e.getMessage());
            close();
            return false;
        }
    }

    private void startReader() {
        final InputStream stream = in;
        reader = new Thread(new Runnable() {
            @Override public void run() {
                byte[] buffer = new byte[2048];

                while (!Thread.currentThread().isInterrupted()) {
                    try {
                        int read = stream.read(buffer);

                        if (read < 0) {
                            break;                       //server closed the connection
                        }

                        if (read > 0) {
                            feed(new String(buffer, 0, read, "UTF-8"));
                        }

                    } catch (java.net.SocketTimeoutException te) {
                        //nothing to read is the normal state, not a fault
                    } catch (Exception e) {
                        break;
                    }
                }
            }
        }, "server-reader");
        reader.setDaemon(true);
        reader.start();
    }

    /**
     * Commands arrive newline terminated, and a read can land mid command or carry several at
     * once, so they are accumulated and split rather than taken one read at a time.
     */
    private void feed(String chunk) {
        pending.append(chunk);

        int nl;

        while ((nl = pending.indexOf("\n")) >= 0) {
            String line = pending.substring(0, nl).trim();
            pending.delete(0, nl + 1);

            if (line.length() > 0 && listener != null) {
                listener.onCommand(line);
            }
        }

        //a server that sends something enormous and never a newline must not grow this forever
        if (pending.length() > 8192) {
            pending.setLength(0);
        }
    }

    public synchronized void close() {
        if (reader != null) {
            reader.interrupt();
            reader = null;
        }

        try { if (socket != null) socket.close(); } catch (Exception ignored) { }

        socket = null;
        out = null;
        in = null;
    }
}
