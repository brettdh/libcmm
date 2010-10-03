package edu.umich.intnw.scout;

import java.net.Socket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;
import java.util.Date;

import android.util.Log;

public class NetworkTest {
    private static String TAG = NetworkTest.class.getName();
    
    final private static short BW_DOWN_PORT = 4321;
    final private static short BW_UP_PORT = 4322;
    final private static short RTT_PORT = 4323;

    final private static int timeoutSecs = 1;
    //final private static long timeoutNanosecs = timeoutSecs*1000*1000*1000;
    
    public String localAddr;
    public String remoteAddr;
    public int bw_down_Bps;
    public int bw_up_Bps;
    public int rtt_ms;

    final private static int MTU = 1500;
    private byte[] bytes;
    
    public NetworkTest(String local) {
        init(local, "141.212.110.132");
    }
    
    public NetworkTest(String local, String remote) {
        init(local, remote);
    }
    
    public void init(String local, String remote) {
        localAddr = local;
        remoteAddr = remote;
        bytes = new byte[MTU];
    }
    
    private Socket setupSocket(short port) throws IOException {
        Log.d(TAG, "Connecting to " + remoteAddr + ":" + port);
        Socket sock = new Socket();
        sock.setSendBufferSize(1); // can't set it to zero; booo
        sock.bind(new InetSocketAddress(InetAddress.getByName(localAddr), 0));
        sock.setSoTimeout(timeoutSecs * 1000);
        sock.connect(new InetSocketAddress(InetAddress.getByName(remoteAddr), port));
        return sock;
    }
    
    private double secondsDiff(long startNanosecs, long endNanosecs) {
        long diffNanosecs = endNanosecs - startNanosecs;
        return ((double)diffNanosecs) / (1000000000.0);
    }
    
    private double secondsDiff(Date start, Date end) {
        long diff_ms = end.getTime() - start.getTime();
        return ((double)diff_ms) / 1000.0;
    }
    
    public class NetworkTestException extends Exception {
        public NetworkTestException(String msg) {
            super(msg);
        }
    };

    private void testDownload() throws NetworkTestException {
        Log.d(TAG, "Starting download test");
        
        try {
            Socket sock = setupSocket(BW_DOWN_PORT);
            //long startTime = System.nanoTime();
            //long endTime = startTime + timeoutNanosecs;
            Date startTime = new Date();
            Date endTime = new Date(startTime.getTime() + timeoutSecs * 1000);
            
            int data = 0;
            InputStream in = sock.getInputStream();
            while (true) {
                if ((new Date()).getTime() > endTime.getTime()) {
                    break;
                }
                int rc;
                rc = in.read(bytes);
                if (rc <= 0) {
                    sock.close();
                    throw new IOException();
                }
                data += rc;
            }
            
            endTime = new Date();;
            sock.close();
            
            bw_down_Bps = (int)((double)data/secondsDiff(startTime, endTime));
        } catch (IOException e) {
            throw new NetworkTestException("Download test failed: " + e.getMessage());
        }
    }
    
    private void testUpload() throws NetworkTestException {
        Log.d(TAG, "Starting upload test");
        
        try {
            Socket sock = setupSocket(BW_UP_PORT);
            //long startTime = System.nanoTime();
            //long endTime = startTime + timeoutNanosecs;
            Date startTime = new Date();
            Date endTime = new Date(startTime.getTime() + timeoutSecs * 1000);
            
            for (byte b : bytes) {
                b = '5';
            }
            int data = 0;
            OutputStream out = sock.getOutputStream();
            while (true) {
                if ((new Date()).getTime() > endTime.getTime()) {
                    break;
                }
                out.write(bytes);
                data += bytes.length;
            }
            
            endTime = new Date();
            sock.close();
            
            bw_up_Bps = (int)((double)data / secondsDiff(startTime, endTime));
        } catch (IOException e) {
            throw new NetworkTestException("Upload test failed: " + e.getMessage());
        }
    }
    
    private void testRTT() throws NetworkTestException {
        Log.d(TAG, "Starting RTT test");
        
        byte[] buf = new byte[4];
        buf[0] = 't';
        buf[1] = 'e';
        buf[2] = 's';
        buf[3] = 't';
        
        byte[] recvBuf = new byte[buf.length];

        try {
            Socket sock = setupSocket(RTT_PORT);
            //long startTime = System.nanoTime();
            //long endTime = startTime + timeoutNanosecs;
            Date startTime = new Date();
            Date endTime = new Date(startTime.getTime() + timeoutSecs * 1000);
            
            InputStream in = sock.getInputStream();
            OutputStream out = sock.getOutputStream();
            int tries = 0;
            while (true) {
                if ((new Date()).getTime() > endTime.getTime()) {
                    break;
                }
                
                out.write(buf);
                int rc = in.read(recvBuf);
                if (rc <= 0) {
                    sock.close();
                    throw new IOException();
                }
                tries++;
            }
            
            endTime = new Date();
            sock.close();
            
            if (tries == 0) {
                // shouldn't happen; socket op should have thrown
                throw new NetworkTestException("RTT test failed; no responses");
            }
            rtt_ms = (int)(secondsDiff(startTime, endTime)*1000 
                           / (double)tries);
        } catch (IOException e) {
            throw new NetworkTestException("RTT test failed: " + e.getMessage());
        }
    }
    
    public void runTests() throws NetworkTestException {
        Log.d(TAG, "Starting tests on " + localAddr);
        testDownload();
        testUpload();
        testRTT();
        Log.d(TAG, "Results: bw_down " + bw_down_Bps +
              " bw_up " + bw_up_Bps + " rtt " + rtt_ms);
    }
};
