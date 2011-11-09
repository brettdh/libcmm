package edu.umich.intnw;

import java.io.IOException;
import java.io.OutputStream;

public class MultisocketOutputStream extends OutputStream {
    private MultiSocket socket;
    
    MultisocketOutputStream(MultiSocket socket) {
        this.socket = socket;
    }

    @Override
    public void write(int oneByte) throws IOException {
        // could implement this, but the performance would be awful
        //  unless I did extra buffering.
        throw new Error("One-byte write not implemented for MultisocketOutputStream!");
    }

    @Override
    public void write(byte[] buffer, int offset, int count) throws IOException {
        write(buffer, offset, count, 0);
    }
    
    public void write(byte[] buffer, int offset, int count, int labels) throws IOException {
        if (offset < 0 || count < 0 || (offset + count) > buffer.length) {
            throw new IndexOutOfBoundsException();
        }
        SystemCalls.ms_write(socket.msock_fd, buffer, offset, count, labels);
    }

    @Override
    public void write(byte[] buffer) throws IOException {
        write(buffer, 0);
    }
    
    public void write(byte[] buffer, int labels) throws IOException {
        write(buffer, 0, buffer.length, labels);
    }
    
    private Object UNIMPLEMENTED_METHODS_MARKER = null;

    @Override
    public void close() throws IOException {
        socket.close();
    }
}
