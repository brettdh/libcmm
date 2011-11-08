package edu.umich.intnw;

import java.io.IOException;
import java.io.InputStream;

public class MultisocketInputStream extends InputStream {
    private int msock_fd;
    
    public MultisocketInputStream(int msock_fd) {
        this.msock_fd = msock_fd;
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
        
        return SystemCalls.ms_read(msock_fd, buffer, offset, length, outLabels);
    }

    public int read(byte[] buffer, int[] outLabels) throws IOException {
        return read(buffer, 0, buffer.length, outLabels);
    }
    
    @Override
    public int read(byte[] buffer) throws IOException {
        return read(buffer, null);
    }
}
