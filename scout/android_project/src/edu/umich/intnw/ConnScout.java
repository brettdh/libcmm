package edu.umich.intnw;

import android.app.Activity;
import android.os.Bundle;
import android.view.View.OnClickListener;
import android.view.View;
import android.widget.TextView;
import android.widget.Button;
import android.content.Intent;

public class ConnScout extends Activity
{
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
    }
    
    OnClickListener mStartScoutListener = new OnClickListener() {
        public void onClick(View v) {
            startService(new Intent(ConnScout.this, ConnScoutService.class));
        }
    };

    OnClickListener mStopScoutListener = new OnClickListener() {
        public void onClick(View v) {
            stopService(new Intent(ConnScout.this, ConnScoutService.class));
        }
    };
    
    public void displayUpdate(String ipAddr, boolean down) {
        
    }
}
