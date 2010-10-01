package edu.umich.intnw.scout;

import java.net.Socket;
import java.net.InetSocketAddress;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.IOException;

public class NetworkTest {
    final private static short BW_DOWN_PORT = 4321;
    final private static short BW_UP_PORT = 4322;
    final private static short RTT_PORT = 4323;

    final private static int timeoutSecs = 1;
    final private static long timeoutNanosecs = timeoutSecs*1000*1000*1000;
    
    public String localAddr;
    public String remoteAddr;
    public int bw_down_Bps;
    public int bw_up_Bps;
    public int rtt_ms;

    final private static int MTU = 1500;
    private byte[] bytes;
    
    public NetworkTest(String local, String remote) {
        localAddr = local;
        remoteAddr = remote;
        bytes = new byte[MTU];
    }
    
    private Socket setupSocket(short port) throws IOException {
        Socket sock = new Socket();
        sock.setSendBufferSize(0);
        sock.bind(new InetSocketAddress(localAddr, 0));
        sock.setSoTimeout(timeoutSecs * 1000);
        sock.connect(new InetSocketAddress(localAddr, port));
        return sock;
    }
    
    private double secondsDiff(long startNanosecs, long endNanosecs) {
        long diffNanosecs = endNanosecs - startNanosecs;
        return ((double)diffNanosecs) / (1000000000.0);
    }
    
    public class NetworkTestException extends Exception {
        public NetworkTestException(String msg) {
            super(msg);
        }
    };

    private void testDownload() throws NetworkTestException {
        try {
            Socket sock = setupSocket(BW_DOWN_PORT);
            long startTime = System.nanoTime();
            long endTime = startTime + timeoutNanosecs;
            
            int data = 0;
            InputStream in = sock.getInputStream();
            while (true) {
                if (System.nanoTime() > endTime) {
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
            
            endTime = System.nanoTime();
            sock.close();
            
            bw_down_Bps = (int)((double)data/secondsDiff(startTime, endTime));
        } catch (IOException e) {
            throw new NetworkTestException("Download test failed");
        }
    }
    
    private void testUpload() throws NetworkTestException {
        try {
            Socket sock = setupSocket(BW_UP_PORT);
            long startTime = System.nanoTime();
            long endTime = startTime + timeoutNanosecs;
            
            for (byte b : bytes) {
                b = '5';
            }
            int data = 0;
            OutputStream out = sock.getOutputStream();
            while (true) {
                if (System.nanoTime() > endTime) {
                    break;
                }
                out.write(bytes);
                data += bytes.length;
            }
            
            endTime = System.nanoTime();
            sock.close();
            
            bw_up_Bps = (int)((double)data / secondsDiff(startTime, endTime));
        } catch (IOException e) {
            throw new NetworkTestException("Upload test failed");
        }
    }
    
    private void testRTT() throws NetworkTestException {
        byte[] buf = new byte[4];
        buf[0] = 't';
        buf[1] = 'e';
        buf[2] = 's';
        buf[3] = 't';
        
        byte[] recvBuf = new byte[buf.length];

        try {
            Socket sock = setupSocket(RTT_PORT);
            long startTime = System.nanoTime();
            long endTime = startTime + timeoutNanosecs;
            
            InputStream in = sock.getInputStream();
            OutputStream out = sock.getOutputStream();
            int tries = 0;
            while (true) {
                if (System.nanoTime() > endTime) {
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
            
            endTime = System.nanoTime();
            sock.close();
            
            if (tries == 0) {
                // shouldn't happen; socket op should have thrown
                throw new NetworkTestException("RTT test failed");
            }
            rtt_ms = (int)(secondsDiff(startTime, endTime)*1000 
                           / (double)tries);
        } catch (IOException e) {
            throw new NetworkTestException("RTT test failed");
        }
    }
    
    public void runTests() throws NetworkTestException {
        testDownload();
        testUpload();
        testRTT();
    }
};
