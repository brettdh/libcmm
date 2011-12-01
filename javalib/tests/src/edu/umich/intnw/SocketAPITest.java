package edu.umich.intnw;

import java.io.IOException;
import java.net.SocketException;
import java.net.SocketTimeoutException;

import android.test.InstrumentationTestCase;

public class SocketAPITest extends InstrumentationTestCase {
    public void testSoTimeoutGetAndSet() throws SocketException {
        MultiSocket sock = new MultiSocket();
        int timeout = sock.getSoTimeout();
        assertEquals(0, timeout);
        
        sock.setSoTimeout(5);
        assertEquals(5, sock.getSoTimeout());
        sock.setSoTimeout(4321);
        assertEquals(4321, sock.getSoTimeout());
        
        sock.setSoTimeout(0);
        assertEquals(0, sock.getSoTimeout());
    }
    
    public void testSocketTimeoutExceptionIsThrown() throws IOException {
        MultiSocket sock = new MultiSocket("141.212.110.132", 4242);
        sock.setSoTimeout(1);
        try {
            byte[] chunk = new byte[4];
            sock.getInputStream().read(chunk);
            fail("Should have thrown SocketTimeoutException");
        } catch (SocketTimeoutException e) {
            // success
        }
    }
}
