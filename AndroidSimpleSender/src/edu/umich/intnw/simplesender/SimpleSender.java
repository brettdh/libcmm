package edu.umich.intnw.simplesender;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.TextView;
import android.widget.Button;

public class SimpleSender extends Activity
{
    private View rootView;
    private TextView fgResponses;
    private TextView bgResponses;
    private int fg_seqno = 0;
    private int bg_seqno = 0;
    private Thread replyThread;
    
    /** Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.main);
        
        rootView = (View) findViewById(R.id.root_view);
        fgResponses = (TextView) findViewById(R.id.fg_responses);
        bgResponses = (TextView) findViewById(R.id.bg_responses);
        
        Button connectButton = (Button) findViewById(R.id.connect);
        Button disconnectButton = (Button) findViewById(R.id.disconnect);
        Button sendFGButton = (Button) findViewById(R.id.send_fg);
        Button sendBGButton = (Button) findViewById(R.id.send_bg);
        connectButton.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                connect("141.212.110.132", (short)4242);
                replyThread = new Thread(new Runnable() {
                    public void run() {
                        runReplyThread();
                    }
                });
                replyThread.start();
            }
        });
        disconnectButton.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                disconnect();
            }
        });
        sendFGButton.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                sendFG(fg_seqno);
                fg_seqno++;
            }
        });
        sendBGButton.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                sendBG(bg_seqno);
                bg_seqno++;
            }
        });
    }
    
    static {
        System.loadLibrary("intnw_ops");
    }
    
    private native void connect(String hostname, short port);
    private native void disconnect();
    private native void sendFG(int seqno);
    private native void sendBG(int seqno);
    
    // must call from new thread
    public native void runReplyThread();
    
    public void displayResponse(final String response,
                                final boolean foreground) {
        rootView.post(new Runnable() {
            public void run() {
                if (foreground) {
                    fgResponses.append(response + "\n");
                } else {
                    bgResponses.append(response + "\n");
                }
            }
        });
    }
}
