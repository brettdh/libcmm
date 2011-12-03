package edu.umich.intnw;

import java.io.IOException;

public class UndeliverableException extends IOException {
    private static final long serialVersionUID = 1L;
    public UndeliverableException() {
        super();
    }
    
    public UndeliverableException(String detailMessage) {
        super(detailMessage);
    }
}
