package edu.umich.intnw;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.net.InetSocketAddress;

import edu.umich.intnw.MultiSocket;
import android.test.InstrumentationTestCase;

public class SmokeTest extends InstrumentationTestCase {
    private static final String TEST_MSG = "testing, testing, testing";
    private static final int CHUNK_SIZE = 40;
    private MultiSocket socket;
    
    public void setUp() throws IOException {
        socket = new MultiSocket();
        socket.connect(new InetSocketAddress("141.212.110.132", 4242));
    }
    
    public void tearDown() throws IOException {
        socket.close();
    }
    
    public void testConnection() throws IOException {
        final byte[] msg = padWithNul(TEST_MSG, CHUNK_SIZE);
        
        OutputStream out = socket.getOutputStream();
        out.write(msg);
        
        byte[] response = new byte[64];
        InputStream in = socket.getInputStream();
        int rc = in.read(response);
        assertEquals(msg.length, rc);
        
        String expected = new String(msg);
        String actual = new String(response, 0, rc);
        assertEquals(expected, actual);
    }

    private byte[] padWithNul(String string, int size) {
        assert(string.length() < size);
        byte[] buf = new byte[size];
        for (int i = 0; i < size; ++i) {
            if (i < string.length()) {
                buf[i] = (byte) string.charAt(i);
            } else {
                buf[i] = 0;
            }
        }
        return buf;
    }
    
    public void testReaderWriter() throws IOException {
        String msg = "01234567890123456789012345678901234567\n\n";
        assertEquals(CHUNK_SIZE, msg.length());
        
        OutputStreamWriter writer = new OutputStreamWriter(socket.getOutputStream());
        writer.write(msg);
        writer.flush();
        
        BufferedReader reader = new BufferedReader(new InputStreamReader(socket.getInputStream()));
        String actual = reader.readLine();
        assertEquals(msg.trim(), actual);
    }
}
