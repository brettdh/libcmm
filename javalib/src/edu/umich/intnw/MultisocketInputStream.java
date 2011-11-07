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
    public int read(byte[] b, int offset, int length) throws IOException {
        if (offset < 0 || length < 0 || (offset + length) > b.length) {
            throw new IndexOutOfBoundsException();
        }
        return SystemCalls.ms_read(msock_fd, b, offset, length);
    }

    @Override
    public int read(byte[] b) throws IOException {
        return read(b, 0, b.length);
    }
}
