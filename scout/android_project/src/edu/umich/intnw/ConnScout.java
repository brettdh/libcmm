package edu.umich.intnw;

import android.app.Activity;
import android.os.Bundle;
import android.view.View.OnClickListener;
import android.view.View;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Button;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.BroadcastReceiver;
import android.content.Context;

import android.content.ServiceConnection;
import android.content.ComponentName;
import android.os.IBinder;
import android.os.Binder;
import android.util.Log;

import java.util.List;

import edu.umich.intnw.NetUpdate;

public class ConnScout extends Activity
{
    private static String TAG = ConnScout.class.getName();
    
    private ScrollView statusFieldScroll;
    private TextView statusField;
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        
        Button startScout = (Button) findViewById(R.id.start_scout);
        Button stopScout = (Button) findViewById(R.id.stop_scout);
        startScout.setOnClickListener(mStartScoutListener);
        stopScout.setOnClickListener(mStopScoutListener);
        
        statusFieldScroll = 
            (ScrollView) findViewById(R.id.status_field_scroll);
        statusField = (TextView) findViewById(R.id.status_field);
        
        Log.d(TAG, "Created ConnScout Activity" + this.toString());
        bindService(new Intent(this, ConnScoutService.class),
                    onService, 0);
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
            List<NetUpdate> updateHistory = appService.getUpdateHistory();
            StringBuilder str = new StringBuilder();
            for (NetUpdate update : updateHistory) {
                str.append(update.toString())
                   .append("\n");
            }
            
            final String allText = str.toString();
            statusFieldScroll.post(new Runnable() {
                public void run() {
                    statusField.setText(allText);
                    statusFieldScroll.fullScroll(ScrollView.FOCUS_DOWN);
                }
            });
        }
        registerReceiver(mReceiver, 
                         new IntentFilter(ConnScoutService.BROADCAST_ACTION));
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
    
    public void displayUpdate(NetUpdate update) {
        final String str = update.toString() + "\n";
        statusFieldScroll.post(new Runnable() {
            public void run() {
                statusField.append(str);
            }
        });
    }
    
    private BroadcastReceiver mReceiver = new BroadcastReceiver() {
        public void onReceive(Context context, Intent intent) {
            Bundle extras = intent.getExtras();
            NetUpdate update = 
                (NetUpdate) extras.get(ConnScoutService.BROADCAST_EXTRA);
            displayUpdate(update);
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
