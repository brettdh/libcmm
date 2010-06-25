package edu.umich.intnw;

import android.app.Service;
import android.widget.Toast;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.os.IBinder;
import edu.umich.intnw.ServiceCompat;

public class ConnScoutService extends ServiceCompat
{
    @Override
    public void onCreate() {
        //mNM = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        super.onCreate();
        int rc = startScoutIPC();
        if (rc < 0) {
            stopSelf();
        } else {
            // TODO: start up network monitoring thread
            showNotification();
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        // do clients need to talk to this service at this level?
        return null;
    }
        
    @Override
    public void onDestroy() {
        stopScoutIPC();
        //mNM.cancel(R.string.service_started);
        Toast.makeText(this, R.string.service_stopped, 
                       Toast.LENGTH_SHORT).show();
        stopForegroundCompat(R.string.service_started);
    }
    
    public native int startScoutIPC();
    public native void stopScoutIPC();
    public native void updateNetwork(String ip_addr,
                                     int bw_down, int bw_up, int rtt,
                                     boolean down);

    static {
        System.loadLibrary("conn_scout");
    }
    
    private void showNotification() {
        // In this sample, we'll use the same text for the ticker and the expanded notification
        CharSequence text = getText(R.string.service_started);

        // Set the icon, scrolling text and timestamp
        Notification notification 
            = new Notification(android.R.drawable.sym_def_app_icon, 
                               text, System.currentTimeMillis());

        // The PendingIntent to launch our activity if the user selects this notification
        PendingIntent contentIntent = PendingIntent.getActivity(
            this, 0, new Intent(this, ConnScout.class), 0
        );

        // Set the info for the views that show in the notification panel.
        notification.setLatestEventInfo(
            this, getText(R.string.scout_service_label),
            text, contentIntent
        );

        // Send the notification.
        // We use a layout id because it is a unique number.  We use it later to cancel.
        //setForeground(true);
        //mNM.notify(R.string.service_started, notification);
        startForegroundCompat(R.string.service_started, notification);
    }
    
    //private NotificationManager mNM;
}