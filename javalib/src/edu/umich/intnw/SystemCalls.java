package edu.umich.intnw;

import java.io.IOException;
import java.net.SocketException;
import java.net.SocketTimeoutException;

class SystemCalls {
    static native int ms_stream_socket();
    static native void ms_connect(int ms_fd, byte[] remoteAddrBytes, int port) throws IOException;
    static native void ms_close(int ms_fd);
    static native void ms_write(int msock_fd, byte[] buffer, int offset, int count, int labels) throws IOException;
    static native int ms_read(int msock_fd, byte[] b, int offset, int length, int[] outLabels) throws IOException;
    
    static native int getsockopt_integer(int msock_Fd, int so_name) throws SocketException;
    
    static native void setsockopt_linger(int msock_fd, boolean on, int timeout) throws SocketException;
    static native void setsockopt_boolean(int msock_fd, int so_name, boolean on) throws SocketException;
    static native void setsockopt_integer(int msock_fd, int so_name, int value) throws SocketException;
    static native void set_receive_timeout(int msock_fd, int timeoutMillis) throws SocketException;
    static native int get_receive_timeout(int msock_fd) throws SocketException;
    
    static native int getPort(int msock_fd);
    static native void shutdownInput(int msock_fd) throws SocketException;
    static native void shutdownOutput(int msock_fd) throws SocketException;

    static {
        System.loadLibrary("cmm");
        System.loadLibrary("intnw_javalib");
    }

    static native void ms_wait_for_input(int msock_fd, int timeoutMillis) throws MultiSocketInterruptedException, SocketTimeoutException, IOException;
    static native void install_interruption_signal_handler(long currentThreadId);
    static native void remove_interruption_signal_handler(long currentThreadId);
    static native void interrupt_waiter(long waitingThreadId);
}
