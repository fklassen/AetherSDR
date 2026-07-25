// Foreground service for the spike: keeps RX audio + the VITA UDP
// stream alive while the screen is off / the activity is backgrounded.
// Runs in the app's process; the Qt event loop and sockets keep working
// as long as the process holds foreground state plus wake + WiFi locks.
package org.aethersdr.companion;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

public class RxForegroundService extends Service {
    private static final String CHANNEL_ID = "aether_rx_audio";
    private static final int NOTIFICATION_ID = 1;

    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;

    public static void start(Context context) {
        Intent intent = new Intent(context, RxForegroundService.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            context.startForegroundService(intent);
        else
            context.startService(intent);
    }

    public static void stop(Context context) {
        context.stopService(new Intent(context, RxForegroundService.class));
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            nm.createNotificationChannel(new NotificationChannel(
                    CHANNEL_ID, "RX audio",
                    NotificationManager.IMPORTANCE_LOW));
        }
        Notification.Builder builder =
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                        ? new Notification.Builder(this, CHANNEL_ID)
                        : new Notification.Builder(this);
        Notification notification = builder
                .setContentTitle("Aether Companion")
                .setContentText("Receiving radio audio")
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setOngoing(true)
                .build();

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK);
        else
            startForeground(NOTIFICATION_ID, notification);

        PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
        wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,
                "aethercompanion:rxaudio");
        wakeLock.acquire();

        WifiManager wm = (WifiManager) getApplicationContext()
                .getSystemService(WIFI_SERVICE);
        wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                "aethercompanion:rxaudio");
        wifiLock.acquire();

        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (wifiLock != null && wifiLock.isHeld())
            wifiLock.release();
        if (wakeLock != null && wakeLock.isHeld())
            wakeLock.release();
        stopForeground(true);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
