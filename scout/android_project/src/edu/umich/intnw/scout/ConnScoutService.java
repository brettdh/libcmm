package edu.umich.intnw.scout;

import android.widget.Toast;
import android.app.Notification;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.IBinder;
import android.os.Binder;
import java.util.Collections;
import java.util.List;
import java.util.LinkedList;
import java.util.Timer;
import java.util.TimerTask;

import edu.umich.intnw.scout.ServiceCompat;
import edu.umich.intnw.scout.ConnectivityListener;

public class ConnScoutService extends ServiceCompat
{
    private static String TAG = ConnScoutService.class.getName();
    
    private ConnectivityListener mListener;
    private Timer mTimer;
    private TimerTask mTask;

    private boolean running = false;
    public boolean isRunning() {
        return running;
    }
    
    @Override
    public void onCreate() {
        super.onCreate();
        updateHistory = 
            Collections.synchronizedList(new LinkedList<NetUpdate>());
    }
    
    @Override
    public void onStart(Intent intent, int startId) {
        super.onStart(intent, startId);

        // This can be called multiple times, even though the service
        //   has already been started.  So, ignore all but the first.
        if (mListener == null) {
            int rc = startScoutIPC();
            if (rc < 0) {
                stopSelf();
            } else {
                mListener = new ConnectivityListener(this);
            
                IntentFilter filter = new IntentFilter();
                filter.addAction(ConnectivityManager.CONNECTIVITY_ACTION);
                filter.addAction(ConnectivityListener.NETWORK_MEASUREMENT_RESULT);
                filter.addAction(ConnectivityListener.ACTION_START_MEASUREMENT);
                registerReceiver(mListener, filter);
            
                running = true;
                showNotification();

                sendBroadcast(new Intent(BROADCAST_START));
            
                mTimer = new Timer();
                mTask = new TimerTask() {
                        public void run() {
                            measureNetworks();
                        }
                    };
            
                // re-measure the networks every 15 minutes or so
                //mTimer.schedule(mTask, 0, 900 * 1000);
            }
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
        if (running) {
            running = false;
            mTimer.cancel();
            
            sendBroadcast(new Intent(BROADCAST_STOP));
            
            unregisterReceiver(mListener);

            stopScoutIPC();
        
            Toast.makeText(this, R.string.service_stopped, 
                           Toast.LENGTH_SHORT).show();
            stopForegroundCompat(R.string.service_started);
        }
    }
    
    public native int startScoutIPC();
    public native void stopScoutIPC();
    public native void updateNetwork(String ip_addr,
                                     int bw_down, int bw_up, int rtt,
                                     boolean down, int networkType);
    
    public void updateNetwork(String ip, int bw_down, int bw_up, int rtt, boolean down) {
        updateNetwork(ip, bw_down, bw_up, rtt, down, 0); // default = unknown network type
    }
               
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
    public static final String BROADCAST_MEASUREMENT_STARTED = 
        "edu.umich.intnw.scout.MeasurementStarted";
    public static final String BROADCAST_MEASUREMENT_DONE = 
        "edu.umich.intnw.scout.MeasurementDone";
    
    public void logUpdate(String ip_addr, NetworkInfo info) {
        logUpdate(ip_addr, info.getType(), info.isConnected());
    }
    
    public void logUpdate(String ip_addr, int type, boolean connected) {
        NetUpdate update = new NetUpdate(ip_addr);
        update.type = type;
        update.connected = connected;
        logUpdate(update);
    }
    
    public void logUpdate(NetUpdate network) {
        updateHistory.add(network);
        
        Intent updateNotification = new Intent(BROADCAST_ACTION);
        updateNotification.putExtra(BROADCAST_EXTRA, network);
        sendBroadcast(updateNotification);
    }
    
    public void measureNetworks() {
        mListener.measureNetworks();
    }
    
    public boolean measurementInProgress() {
        return mListener != null && mListener.measurementInProgress();
    }
    
    public void reportMeasurementFailure(int type, String ipAddr) {
        StringBuilder msg = new StringBuilder();
        msg.append("Measurement for ");
        if (type == ConnectivityManager.TYPE_WIFI) {
            msg.append("wifi");
        } else {
            msg.append("cellular");
        }
        msg.append(" network ").append(ipAddr).append(" failed.");
        measurementDone(msg.toString());
    }
    
    public void measurementDone(String msg) {
        Intent notification = new Intent(BROADCAST_MEASUREMENT_DONE);
        if (msg != null) {
            notification.putExtra(BROADCAST_EXTRA, msg);
        }
        sendBroadcast(notification);
    }

    static {
        System.loadLibrary("native_networktest");
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
