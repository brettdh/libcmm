package edu.umich.intnw;

import java.io.InterruptedIOException;

public class MultiSocketInterruptedException extends InterruptedIOException {
    private static final long serialVersionUID = 2L;
    
    public MultiSocketInterruptedException() {
        super();
    }
    
    public MultiSocketInterruptedException(String detailMessage) {
        super(detailMessage);
    }
}
