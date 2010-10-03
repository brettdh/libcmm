package edu.umich.intnw.scout;

import android.app.Activity;
import android.os.Bundle;
import android.view.View.OnClickListener;
import android.view.View;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Button;
import android.widget.Toast;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.net.ConnectivityManager;

import android.content.ServiceConnection;
import android.content.ComponentName;
import android.os.IBinder;
import android.os.Binder;
import android.util.Log;

import java.util.List;
import java.util.Date;

import edu.umich.intnw.scout.NetUpdate;
import edu.umich.intnw.scout.Utilities;

public class ConnScout extends Activity
{
    private static String TAG = ConnScout.class.getName();

    private View rootView;
    private TextView mobileAddr;
    private TextView wifiAddr;
    private TextView mobileStats;
    private TextView wifiStats;
    private ScrollView statusFieldScroll;
    private TextView statusField;
    private Button startScout;
    private Button stopScout;
    private Button measureButton;
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        
        rootView = (View) findViewById(R.id.root_view);
        
        startScout = (Button) findViewById(R.id.start_scout);
        stopScout = (Button) findViewById(R.id.stop_scout);
        measureButton = (Button) findViewById(R.id.measure);
        startScout.setOnClickListener(mStartScoutListener);
        stopScout.setOnClickListener(mStopScoutListener);
        measureButton.setOnClickListener(mMeasureListener);
        
        mobileAddr = (TextView) findViewById(R.id.mobile_addr);
        wifiAddr = (TextView) findViewById(R.id.wifi_addr);
        mobileStats = (TextView) findViewById(R.id.mobile_stats);
        wifiStats = (TextView) findViewById(R.id.wifi_stats);
        
        statusFieldScroll = 
            (ScrollView) findViewById(R.id.status_field_scroll);
        statusField = (TextView) findViewById(R.id.status_field);
        
        initFields();
        
        Log.d(TAG, "Created ConnScout Activity" + this.toString());
        bindService(new Intent(this, ConnScoutService.class),
                    onService, 0);
                    
        startScout.setEnabled(appService == null);
        stopScout.setEnabled(appService != null);
        measureButton.setEnabled(appService != null);
    }
    
    @Override
    public void onDestroy() {
        super.onDestroy();
        
        Log.d(TAG, "Destroyed ConnScout Activity" + this.toString());
        unbindService(onService);
    }
    
    @Override
    public void onResume(){
        super.onResume();
        
        if (appService != null) {
            NetUpdate lastCellularUpdate = null;
            NetUpdate lastWifiUpdate = null;
            List<NetUpdate> updateHistory = appService.getUpdateHistory();
            StringBuilder str = new StringBuilder();
            for (NetUpdate update : updateHistory) {
                str.append(update.toString())
                   .append("\n");
                if (update.type == ConnectivityManager.TYPE_WIFI) {
                    lastWifiUpdate = update;
                } else {
                    lastCellularUpdate = update;
                }
            }
            
            final String allText = str.toString();
            statusFieldScroll.post(new Runnable() {
                public void run() {
                    statusField.setText(allText);
                    statusFieldScroll.fullScroll(ScrollView.FOCUS_DOWN);
                }
            });
            
            displayUpdate(lastCellularUpdate);
            displayUpdate(lastWifiUpdate);
        }
        
        IntentFilter filter = new IntentFilter();
        filter.addAction(ConnScoutService.BROADCAST_ACTION);
        filter.addAction(ConnScoutService.BROADCAST_START);
        filter.addAction(ConnScoutService.BROADCAST_STOP);
        filter.addAction(ConnScoutService.BROADCAST_MEASUREMENT_DONE);
        registerReceiver(mReceiver, filter);
    }
    
    @Override
    public void onPause() {
        super.onPause();
        
        unregisterReceiver(mReceiver);
    }
    
    OnClickListener mStartScoutListener = new OnClickListener() {
        public void onClick(View v) {
            bindService(new Intent(ConnScout.this, ConnScoutService.class),
                        onService, BIND_AUTO_CREATE);
            startService(new Intent(ConnScout.this, ConnScoutService.class));
        }
    };

    OnClickListener mStopScoutListener = new OnClickListener() {
        public void onClick(View v) {
            stopService(new Intent(ConnScout.this, ConnScoutService.class));
            if (appService != null) {
                unbindService(onService);
                appService =  null;
            }
        }
    };
    
    OnClickListener mMeasureListener = new OnClickListener() {
        public void onClick(View v) {
            if (appService != null) {
                ((Button) v).setText("Measuring...");
                v.setEnabled(false);
                appService.measureNetworks();
            } else {
                Toast.makeText(ConnScout.this, 
                               "No scout service (shouldn't happen)", 
                               Toast.LENGTH_SHORT).show();
            }
        }
    };
    
    public void appendToStatusField(final String str) {
        statusFieldScroll.post(new Runnable() {
            public void run() {
                statusField.append(str);
                statusFieldScroll.fullScroll(ScrollView.FOCUS_DOWN);
            }
        });
    }
    
    public void displayUpdate(final NetUpdate update) {
        if (update == null) {
            return;
        }
        
        rootView.post(new Runnable() {
            public void run() {
                final String addr;
                if (update.connected) {
                    addr = update.ipAddr;
                } else {
                    addr = new String("(unavailable)");
                }
                
                if (update.type == ConnectivityManager.TYPE_WIFI) {
                    wifiAddr.setText(addr);
                    if (update.hasStats()) {
                        wifiStats.setText(update.statsString());
                    }
                } else {
                    mobileAddr.setText(addr);
                    if (update.hasStats()) {
                        mobileStats.setText(update.statsString());
                    }
                }
            }
        });
    }
    
    public void initFields() {
        rootView.post(new Runnable() {
            public void run() {
                mobileAddr.setText("(unavailable)");
                wifiAddr.setText("(unavailable)");
            }
        });
    }
    
    private BroadcastReceiver mReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (action.equals(ConnScoutService.BROADCAST_ACTION)) {
                Bundle extras = intent.getExtras();
                NetUpdate update = 
                    (NetUpdate) extras.get(ConnScoutService.BROADCAST_EXTRA);
                displayUpdate(update);
                appendToStatusField(update.toString() + "\n");
            } else if (action.equals(ConnScoutService.BROADCAST_START)) {
                appendToStatusField(Utilities.formatTimestamp(new Date()) +
                                    " Scout started\n");
                startScout.setEnabled(false);
                stopScout.setEnabled(true);
                measureButton.setEnabled(true);
            } else if (action.equals(ConnScoutService.BROADCAST_STOP)) {
                appendToStatusField(Utilities.formatTimestamp(new Date()) +
                                    " Scout stopped\n");
                startScout.setEnabled(true);
                stopScout.setEnabled(false);
                measureButton.setEnabled(false);
            } else if (action.equals(ConnScoutService.BROADCAST_MEASUREMENT_DONE)) {
                measureButton.setText("Measure");
                measureButton.setEnabled(true);
                Bundle extras = intent.getExtras();
                if (extras != null) {
                    String msg = 
                        (String) extras.get(ConnScoutService.BROADCAST_EXTRA);
                    if (msg != null) {
                        Toast.makeText(ConnScout.this, msg,
                                       Toast.LENGTH_SHORT).show();
                    }
                }
            } else {
                // ignore; unknown
            }
        }
    };
    
    private ConnScoutService appService = null;
    private ServiceConnection onService=new ServiceConnection() {
        public void onServiceConnected(ComponentName className,
                                       IBinder rawBinder) {
            appService = 
                ((ConnScoutService.LocalBinder)rawBinder).getService();
        }

        public void onServiceDisconnected(ComponentName className) {
            appService = null;
        }
    };
}
