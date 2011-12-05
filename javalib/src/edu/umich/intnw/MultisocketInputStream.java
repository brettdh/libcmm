package edu.umich.intnw;

import java.io.IOException;
import java.io.InputStream;
import java.net.SocketTimeoutException;
import java.util.Collections;
import java.util.HashSet;

public class MultisocketInputStream extends InputStream {
    private MultiSocket socket;
    
    public MultisocketInputStream(MultiSocket socket) {
        super();
        this.socket = socket;
    }

    @Override
    public int read() throws IOException {
        byte[] bytes = new byte[1];
        int rc = read(bytes);
        if (rc < 0) {
            return rc;
        }
        return bytes[0];
    }

    @Override
    public int read(byte[] buffer, int offset, int length) throws IOException {
        return read(buffer, offset, length, null);
    }
    
    public int read(byte[] buffer, int offset, int length, int[] outLabels) throws IOException {
        if (offset < 0 || length < 0 || (offset + length) > buffer.length) {
            throw new IndexOutOfBoundsException();
        }
        
        return SystemCalls.ms_read(socket.msock_fd, buffer, offset, length, outLabels);
    }

    public int read(byte[] buffer, int[] outLabels) throws IOException {
        return read(buffer, 0, buffer.length, outLabels);
    }
    
    @Override
    public int read(byte[] buffer) throws IOException {
        return read(buffer, null);
    }
    
    public void waitForInput() throws MultiSocketInterruptedException, IOException {
        waitforInput(0);
    }
    
    private HashSet<Thread> waiters = new HashSet<Thread>();
    private boolean interrupted = false;
    
    /**
     * Respects the timeout set by setSoTimeout().  Waits for min(getSoTimeout(), timeoutMillis) if timeoutMillis is > 0.
     * @param timeoutMillis
     * @throws MultiSocketInterruptedException
     * @throws SocketTimeoutException
     * @throws IOException
     */
    public void waitforInput(int timeoutMillis) throws MultiSocketInterruptedException, SocketTimeoutException, IOException {
        timeoutMillis = Math.max(0, timeoutMillis);
        int socketTimeout = socket.getSoTimeout();
        if (timeoutMillis > 0 && socketTimeout > 0) {
            timeoutMillis = Math.min(timeoutMillis, socketTimeout);
        } else {
            // only one or neither are non-zero.
            // pick that one or zero.
            timeoutMillis = Math.max(timeoutMillis, socketTimeout);
        }
        
        synchronized(waiters) {
            if (this.interrupted) {
                try {
                    SystemCalls.ms_wait_for_input(socket.msock_fd, 1);
                    return; // if ms_wait_for_input doesn't time out, 
                            // we already have buffered data ready to return.
                } catch (SocketTimeoutException e) {
                    // ...if it does time out, then we are interrupted 
                    //  and we have no more data ready to return.
                    throw new MultiSocketInterruptedException("MultiSocket was in interrupted state before wait!");
                }
            }
            waiters.add(Thread.currentThread());
        }
        try {
            SystemCalls.install_interruption_signal_handler(Thread.currentThread().getId());
            SystemCalls.ms_wait_for_input(socket.msock_fd, timeoutMillis);
        } finally {
            synchronized(waiters) {
                waiters.remove(Thread.currentThread());
            }
            SystemCalls.remove_interruption_signal_handler(Thread.currentThread().getId());
        }
    }

    public void interruptWaiters() {
        synchronized(waiters) {
            for (Thread waiter : waiters) {
                SystemCalls.interrupt_waiter(waiter.getId());
            }
            this.interrupted = true; // interrupted state persists until cleared by another method
        }
    }
    
    public void clearInterruptedState() {
        synchronized(waiters) {
            this.interrupted = false;
        }
    }

    private Object UNIMPLEMENTED_METHODS_MARKER = null;
    
    @Override
    public int available() throws IOException {
        throw new Error("available not supported for multisocket input stream!");
    }

    @Override
    public void close() throws IOException {
        socket.close();
    }
}
