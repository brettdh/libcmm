package edu.umich.intnw.scout;

import android.app.Service;
import android.widget.Toast;
import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.IBinder;
import android.os.Binder;
import android.os.Parcelable;
import android.os.Parcel;

import java.util.Collections;
import java.util.List;
import java.util.LinkedList;
import java.util.Date;

import edu.umich.intnw.scout.ServiceCompat;
import edu.umich.intnw.scout.ConnectivityListener;

public class ConnScoutService extends ServiceCompat
{
    private static String TAG = ConnScoutService.class.getName();
    
    private ConnectivityListener mListener;

    @Override
    public void onCreate() {
        //mNM = (NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        super.onCreate();
        int rc = startScoutIPC();
        if (rc < 0) {
            stopSelf();
        } else {
            updateHistory = 
                Collections.synchronizedList(new LinkedList<NetUpdate>());
            
            // TODO: start up network monitoring
            mListener = new ConnectivityListener(this);
            
            IntentFilter filter = new IntentFilter();
            filter.addAction(ConnectivityManager.CONNECTIVITY_ACTION);
            registerReceiver(mListener, filter);
            
            
            showNotification();

            sendBroadcast(new Intent(BROADCAST_START));
        }
    }

    public class LocalBinder extends Binder {
        public ConnScoutService getService() {
            return ConnScoutService.this;
        }
    };

    private final Binder binder=new LocalBinder();
    @Override
    public IBinder onBind(Intent intent) {
        // do clients need to talk to this service at this level?
        // Yes; the ConnScout Activity does.
        return binder;
    }
        
    @Override
    public void onDestroy() {
        sendBroadcast(new Intent(BROADCAST_STOP));
        
        unregisterReceiver(mListener);
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
               
    private List<NetUpdate> updateHistory;
    public List<NetUpdate> getUpdateHistory() {
        return updateHistory;
    }
    
    public static final String BROADCAST_ACTION = 
        "edu.umich.intnw.scout.NetworkUpdateEvent";
    public static final String BROADCAST_START = 
        "edu.umich.intnw.scout.ScoutStartEvent";
    public static final String BROADCAST_STOP = 
        "edu.umich.intnw.scout.ScoutStopEvent";
    public static final String BROADCAST_EXTRA = 
        "edu.umich.intnw.scout.NetworkUpdateExtra";
    
    public void logUpdate(String ip_addr, NetworkInfo info) {
        NetUpdate update = new NetUpdate();
        update.timestamp = new Date();
        update.ipAddr = ip_addr;
        update.info = info;
        updateHistory.add(update);
        
        Intent updateNotification = new Intent(BROADCAST_ACTION);
        updateNotification.putExtra(BROADCAST_EXTRA, update);
        sendBroadcast(updateNotification);
    }

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

        Intent appIntent = new Intent(this, ConnScout.class);
        // The PendingIntent to launch our activity if the user selects this notification
        PendingIntent contentIntent = PendingIntent.getActivity(
            this, 0, appIntent, 0
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
