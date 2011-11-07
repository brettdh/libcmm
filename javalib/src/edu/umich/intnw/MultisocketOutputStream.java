package edu.umich.intnw;

import java.io.IOException;
import java.io.OutputStream;

public class MultisocketOutputStream extends OutputStream {
    private int msock_fd;
    
    MultisocketOutputStream(int msock_fd) {
        this.msock_fd = msock_fd;
    }

    @Override
    public void write(int oneByte) throws IOException {
        // could implement this, but the performance would be awful
        //  unless I did extra buffering.
        throw new Error("One-byte write not implemented for MultisocketOutputStream!");
    }

    @Override
    public void write(byte[] buffer, int offset, int count) throws IOException {
        if (offset < 0 || count < 0 || (offset + count) > buffer.length) {
            throw new IndexOutOfBoundsException();
        }
        SystemCalls.ms_write(msock_fd, buffer, offset, count);
    }

    @Override
    public void write(byte[] buffer) throws IOException {
        write(buffer, 0, buffer.length);
    }
}
