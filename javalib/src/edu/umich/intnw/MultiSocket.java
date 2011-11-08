package edu.umich.intnw;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.nio.channels.SocketChannel;

public class MultiSocket extends Socket {
    private int msock_fd;
    private InputStream inputStream;
    private OutputStream outputStream;
    
    public MultiSocket() {
        msock_fd = SystemCalls.ms_stream_socket();
    }
    
    public MultiSocket(String host, int port) throws IOException {
        msock_fd = SystemCalls.ms_stream_socket();
        
        SocketAddress remoteAddr = new InetSocketAddress(host, port);
        connect(remoteAddr);
    }

    @Override
    public void bind(SocketAddress localAddr) throws IOException {
        //super.bind(localAddr);
        throw new Error("Multisocket bind not implemented!");
    }

    @Override
    public synchronized void close() throws IOException {
        SystemCalls.ms_close(msock_fd);
        msock_fd = -1;
    }

    @Override
    public void connect(SocketAddress remoteAddr, int timeout)
            throws IOException {
        //super.connect(remoteAddr, timeout);
        throw new Error("Multisocket connect with timeout not implemented!");
    }

    @Override
    public void connect(SocketAddress remoteAddr) throws IOException {
        InetSocketAddress inetAddr = (InetSocketAddress) remoteAddr;
        SystemCalls.ms_connect(msock_fd, inetAddr.getAddress().getAddress(), inetAddr.getPort());
    }

    @Override
    public SocketChannel getChannel() {
        //return super.getChannel();
        throw new Error("Multisocket getChannel not implemented!");
    }

    @Override
    public InputStream getInputStream() throws IOException {
        if (inputStream == null) {
            inputStream = new MultisocketInputStream(msock_fd);
        }
        return inputStream;
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        if (outputStream == null) {
            outputStream = new MultisocketOutputStream(msock_fd);
        }
        return outputStream;
    }
}
