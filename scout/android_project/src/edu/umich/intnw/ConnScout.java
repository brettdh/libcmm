package edu.umich.intnw;

import android.app.Activity;
import android.os.Bundle;
import android.view.View.OnClickListener;
import android.view.View;
import android.widget.TextView;
import android.widget.Button;

public class ConnScout extends Activity
{
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        mStatusField = (TextView) findViewById(R.id.status_field);
        
        Button startScout = (Button) findViewById(R.id.start_scout);
        Button stopScout = (Button) findViewById(R.id.stop_scout);
        startScout.setOnClickListener(mStartScoutListener);
        stopScout.setOnClickListener(mStopScoutListener);
    }
    
    OnClickListener mStartScoutListener = new OnClickListener() {
        public void onClick(View v) {
            int rc = startScoutIPC();
            if (rc < 0) {
                mStatusField.setText("Scout failed to start!");
            } else {
                mStatusField.setText("Scout started successfully.");
            }
        }
    };

    OnClickListener mStopScoutListener = new OnClickListener() {
        public void onClick(View v) {
            stopScoutIPC();
            mStatusField.setText("Stopped scout.  Maybe it worked?");
        }
    };
    
    private TextView mStatusField;
    
    public native int startScoutIPC();
    public native void stopScoutIPC();
    public native void updateNetwork(String ip_addr,
                                     int bw_down, int bw_up, int rtt,
                                     boolean down);

    static {
        System.loadLibrary("conn_scout");
    }
}
