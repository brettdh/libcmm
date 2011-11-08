package edu.umich.intnw;

import java.io.IOException;

class SystemCalls {
    static native int ms_stream_socket();
    static native void ms_connect(int ms_fd, byte[] remoteAddrBytes, int port) throws IOException;
    static native void ms_close(int ms_fd);
    static native void ms_write(int msock_fd, byte[] buffer, int offset, int count, int labels) throws IOException;
    static native int ms_read(int msock_fd, byte[] b, int offset, int length, int[] outLabels) throws IOException;
    
    static {
        System.loadLibrary("cmm");
        System.loadLibrary("intnw_javalib");
    }
}
