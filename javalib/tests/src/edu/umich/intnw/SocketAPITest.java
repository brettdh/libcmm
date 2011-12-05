package edu.umich.intnw;

import java.io.IOException;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.util.ArrayList;
import java.util.Arrays;

import android.test.InstrumentationTestCase;

public class SocketAPITest extends InstrumentationTestCase {
    private static final String TEST_SERVER_IP = "141.212.110.132";
    private static final int TEST_PORT = 4242;
    private MultiSocket sock;

    protected void setUp() throws IOException {
        sock = new MultiSocket(TEST_SERVER_IP, TEST_PORT);
    }
    
    protected void tearDown() throws IOException {
        sock.close();
    }
    
    public void testSoTimeoutGetAndSet() throws SocketException {
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
        sock.setSoTimeout(1);
        try {
            byte[] chunk = new byte[4];
            sock.getInputStream().read(chunk);
            fail("Should have thrown SocketTimeoutException");
        } catch (SocketTimeoutException e) {
            // success
        }
    }
    
    public void testWaitForInputReturnsOnlyWhenInputReady() throws IOException {
        MultisocketInputStream in = (MultisocketInputStream) sock.getInputStream();
        try {
            in.waitforInput(1000);
            fail("waitForInput should have timed out");
        } catch (SocketTimeoutException e) {
            // success
        }
        
        writeChunk();

        in.waitForInput();
        // success
        readChunk(in);
    }

    private void readChunk(MultisocketInputStream in) throws IOException {
        byte[] response = new byte[CHUNKLEN];
        int rc = in.read(response);
        assertTrue(rc == CHUNKLEN);
    }

    final int CHUNKLEN = 40;
    private void writeChunk() throws IOException {
        final String msg = "abcd\n";
        
        byte[] netMessage = new byte[CHUNKLEN];
        Arrays.fill(netMessage, (byte) 0);
        for (int i = 0; i < msg.length(); ++i) {
            netMessage[i] = msg.getBytes()[i];
        }
        sock.getOutputStream().write(netMessage);
    }
    
    public void testWaitForInputIsInterruptible() throws IOException {
        final MultisocketInputStream in = (MultisocketInputStream) sock.getInputStream();
        new Thread() {
            @Override
            public void run() {
                try {
                    Thread.sleep(1000);
                    in.interruptWaiters();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                } catch (SecurityException e) {
                    e.printStackTrace();
                }
            }
        }.start();
        
        try {
            in.waitForInput();
            fail("waitForInput should not return with no ready input");
        } catch (MultiSocketInterruptedException e) {
            // success
        }
    }
    
    private class SocketWaiter extends Thread {
        private MultisocketInputStream in;
        private boolean interrupted;
        public SocketWaiter(MultisocketInputStream in) {
            this.in = in;
            interrupted = false;
        }
        
        @Override
        public void run() {
            try {
                in.waitForInput();
            } catch (MultiSocketInterruptedException e) {
                interrupted = true;
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }
    
    public void testMultipleInputWaiters() throws IOException, InterruptedException {
        MultisocketInputStream in = (MultisocketInputStream) sock.getInputStream();
        ArrayList<SocketWaiter> waiters = new ArrayList<SocketWaiter>();
        for (int i = 0; i < 5; ++i) {
            final SocketWaiter waiter = new SocketWaiter(in);
            waiter.start();
            waiters.add(waiter);
        }
        
        Thread.sleep(3000);
        in.interruptWaiters();
        for (SocketWaiter waiter : waiters) {
            waiter.join();
            assertTrue(waiter.interrupted);
        }
    }
    
    public void testWaitForInputReturnsWhenSocketIsClosed() throws IOException {
        new Thread() {
            @Override
            public void run() {
                try {
                    Thread.sleep(1000);
                    sock.close();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }.start();
        
        MultisocketInputStream in = (MultisocketInputStream) sock.getInputStream();
        in.waitForInput();
        // success; failure detection is left to the next read()
    }
    
    public void testWaitForInputAfterInterruptShouldThrow() throws IOException {
        MultisocketInputStream in = (MultisocketInputStream) sock.getInputStream();
        in.interruptWaiters();
        try {
            in.waitforInput(1000);
            fail("Should have thrown interrupted exception");
        } catch (MultiSocketInterruptedException e) {
            // success
        } catch (SocketTimeoutException e) {
            fail("Should have been interrupted, not timed out");
        }
        
        in.clearInterruptedState();
        try {
            in.waitforInput(1000);
            fail("Should have thrown timeout exception");
        } catch (MultiSocketInterruptedException e) {
            fail("Should have timed out, not have been interrupted");
        } catch (SocketTimeoutException e) {
            // success
        }
    }
    
    public void testWaitForInputStreamReturnsAvailableDataWhenInterrupted() throws IOException, InterruptedException {
        writeChunk();
        Thread.sleep(3000);
        
        MultisocketInputStream in = (MultisocketInputStream) sock.getInputStream();
        in.interruptWaiters();
        in.waitForInput();
        readChunk(in);
        
        try {
            in.waitForInput();
            fail("Should be interrupted after returning buffered data");
        } catch (MultiSocketInterruptedException e) {
            // success
        }
    }
}
