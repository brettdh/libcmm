package edu.umich.intnw.scout;


import java.net.SocketException;

import edu.umich.mobility.networktest.NativeNetworkTest;

import android.util.Log;

public class NetworkTest {
    private static String TAG = NetworkTest.class.getName();
    
    final private static int timeoutSecs = 5;
    
    public String localAddr;
    public String remoteAddr;
    public int bw_down_Bps;
    public int bw_up_Bps;
    public int rtt_ms;

    public NetworkTest(String local) {
        init(local, "141.212.110.115");
    }
    
    public NetworkTest(String local, String remote) {
        init(local, remote);
    }
    
    public void init(String local, String remote) {
        localAddr = local;
        remoteAddr = remote;
    }
    
    public class NetworkTestException extends Exception {
        public NetworkTestException(String msg) {
            super(msg);
        }
    };

    public void runTests() throws NetworkTestException {
        Log.d(TAG, "Starting tests on " + localAddr);
        NativeNetworkTest nativeTest = new NativeNetworkTest(localAddr, remoteAddr);
        
        try {
            Log.d(TAG, "Starting download test");
            bw_down_Bps = nativeTest.runDownloadTest(timeoutSecs);
            Log.d(TAG, "Starting upload test");
            bw_up_Bps = nativeTest.runUploadTest(timeoutSecs);
            Log.d(TAG, "Starting RTT test");
            rtt_ms = nativeTest.runRTTTest();
            Log.d(TAG, "Results: bw_down " + bw_down_Bps +
                  " bw_up " + bw_up_Bps + " rtt " + rtt_ms);
        } catch (SocketException e) {
            throw new NetworkTestException(e.getMessage());
        }
    }
};
