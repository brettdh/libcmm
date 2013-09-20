package edu.umich.intnw;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketAddress;
import java.net.SocketException;
import java.net.SocketOptions;
import java.nio.channels.SocketChannel;

public class MultiSocket extends Socket {
    int msock_fd;
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
    
    /**
     * Returns this multisocket's integer file descriptor, so that it can be passed
     * to JNI code for further use.
     * @return integer file descriptor for this multisocket
     */
    public int getFileDescriptor() {
        return msock_fd;
    }

    @Override
    public void bind(SocketAddress localAddr) throws IOException {
        // multisocket bind has no effect, since multisockets use all available networks.
        // XXX: might need to at least call bind on the fake socket FD, so that
        // XXX: getsockname returns the right address afterwards.
    }

    @Override
    public synchronized void close() throws IOException {
        SystemCalls.ms_close(msock_fd);
        msock_fd = -1;
    }
    
    @Override
    public boolean isClosed() {
        return (msock_fd == -1);
    }

    @Override
    public void setSoLinger(boolean on, int timeout) throws SocketException {
        SystemCalls.setsockopt_linger(msock_fd, on, timeout);
    }

    @Override
    public synchronized void setSoTimeout(int timeoutMillis) throws SocketException {
        SystemCalls.set_receive_timeout(msock_fd, timeoutMillis);
    }

    @Override
    public synchronized int getSoTimeout() throws SocketException {
        // we set this as zero because we never actually do anything for setSoTimeout (see above).
        //  according to the JavaDoc, zero means infinite timeout.
        return SystemCalls.get_receive_timeout(msock_fd);
    }

    @Override
    public void setTcpNoDelay(boolean on) throws SocketException {
        // Just kidding.  Don't actually set it, because it needs to be always set.
        //SystemCalls.setsockopt_boolean(msock_fd, SocketOptions.TCP_NODELAY, on);
    }

    @Override
    public void connect(SocketAddress remoteAddr, int timeout)
            throws IOException {
        connect(remoteAddr); // XXX: ignore timeout.  this should never time out in my tests anyway.
        //throw new Error("Multisocket connect with timeout not implemented!");
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
            inputStream = new MultisocketInputStream(this);
        }
        return inputStream;
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        if (outputStream == null) {
            outputStream = new MultisocketOutputStream(this);
        }
        return outputStream;
    }
    
    @Override
    public synchronized int getReceiveBufferSize() throws SocketException {
        return SystemCalls.getsockopt_integer(msock_fd, SocketOptions.SO_RCVBUF);
    }

    @Override
    public synchronized void setSendBufferSize(int size) throws SocketException {
        // ignoring this for now; I have my own ways of setting the underlying buffer sizes.
        //SystemCalls.setsockopt_integer(msock_fd, SocketOptions.SO_SNDBUF, size);
    }

    @Override
    public int getPort() {
        return SystemCalls.getPort(msock_fd);
    }

    @Override
    public void shutdownInput() throws IOException {
        SystemCalls.shutdownInput(msock_fd);
    }

    @Override
    public void shutdownOutput() throws IOException {
        SystemCalls.shutdownOutput(msock_fd);
    }

    private final Object UNIMPLEMENTED_METHODS_MARKER = null;

    @Override
    public InetAddress getInetAddress() {
        throw new Error("getInetAddress not implemented for multisocket!");
    }

    @Override
    public boolean getKeepAlive() throws SocketException {
        throw new Error("getKeepAlive not implemented for multisocket!");
    }

    @Override
    public InetAddress getLocalAddress() {
        throw new Error("getLocalAddress not implemented for multisocket!");
    }

    @Override
    public int getLocalPort() {
        throw new Error("getLocalPort not implemented for multisocket!");
    }

    @Override
    public SocketAddress getLocalSocketAddress() {
        throw new Error("getLocalSocketAddress not implemented for multisocket!");
    }

    @Override
    public boolean getOOBInline() throws SocketException {
        throw new Error("getOOBInline not implemented for multisocket!");
    }

    @Override
    public SocketAddress getRemoteSocketAddress() {
        throw new Error("getRemoteSocketAddress not implemented for multisocket!");
    }

    @Override
    public boolean getReuseAddress() throws SocketException {
        throw new Error("getReuseAddress not implemented for multisocket!");
    }

    @Override
    public synchronized int getSendBufferSize() throws SocketException {
        throw new Error("getSendBufferSize not implemented for multisocket!");
    }

    @Override
    public int getSoLinger() throws SocketException {
        throw new Error("getSoLinger not implemented for multisocket!");
    }

    @Override
    public boolean getTcpNoDelay() throws SocketException {
        throw new Error("getTcpNoDelay not implemented for multisocket!");
    }

    @Override
    public int getTrafficClass() throws SocketException {
        throw new Error("getTrafficClass not implemented for multisocket!");
    }

    @Override
    public boolean isBound() {
        throw new Error("isBound not implemented for multisocket!");
    }

    @Override
    public boolean isConnected() {
        throw new Error("isConnected not implemented for multisocket!");
    }

    @Override
    public boolean isInputShutdown() {
        throw new Error("isInputShutdown not implemented for multisocket!");
    }

    @Override
    public boolean isOutputShutdown() {
        throw new Error("isOutputShutdown not implemented for multisocket!");
    }

    @Override
    public void sendUrgentData(int value) throws IOException {
        throw new Error("sendUrgentData not implemented for multisocket!");
    }

    @Override
    public void setKeepAlive(boolean keepAlive) throws SocketException {
        throw new Error("setKeepAlive not implemented for multisocket!");
    }

    @Override
    public void setOOBInline(boolean oobinline) throws SocketException {
        throw new Error("setOOBInline not implemented for multisocket!");
    }

    @Override
    public void setPerformancePreferences(int connectionTime, int latency,
                                          int bandwidth) {
        throw new Error("setPerformancePreferences not implemented for multisocket!");
    }

    @Override
    public synchronized void setReceiveBufferSize(int size) throws SocketException {
        throw new Error("setReceiveBufferSize not implemented for multisocket!");
    }

    @Override
    public void setReuseAddress(boolean reuse) throws SocketException {
        throw new Error("setReuseAddress not implemented for multisocket!");
    }

    @Override
    public void setTrafficClass(int value) throws SocketException {
        throw new Error("setTrafficClass not implemented for multisocket!");
    }

    @Override
    public String toString() {
        throw new Error("toString not implemented for multisocket!");
    }
}
