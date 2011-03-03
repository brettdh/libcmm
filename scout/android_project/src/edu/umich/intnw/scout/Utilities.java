package edu.umich.intnw.scout;

import android.util.Log;
import android.net.wifi.WifiInfo;

import java.util.Date;
import java.util.Calendar;
import java.util.GregorianCalendar;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.IOException;
import java.net.NetworkInterface;
import java.net.InetAddress;
import java.util.Enumeration;
import java.net.SocketException;
import java.net.UnknownHostException;


public class Utilities {
    private static String TAG = Utilities.class.getName();
    
    /**
     * Append the formatted timestamp to str, as [hh:mm:ss]
     */
    public static String formatTimestamp(Date timestamp) {
        Calendar cal = new GregorianCalendar();
        cal.setTime(timestamp);
        StringBuilder str = new StringBuilder();
        str.append("[")
           .append(cal.get(Calendar.HOUR_OF_DAY))
           .append(":")
           .append(cal.get(Calendar.MINUTE))
           .append(":")
           .append(cal.get(Calendar.SECOND))
           .append("]");
        return str.toString();
    }
    
    public static String intToIp(int i) {
        InetAddress ip = intToInetAddress(i);
        Log.d(TAG, "intToIp: equivalent InetAddress: " + ip.getHostAddress());
        String ret = ((i >>  0 ) & 0xFF) + "." +
                     ((i >>  8 ) & 0xFF) + "." +
                     ((i >> 16 ) & 0xFF) + "." +
                     ((i >> 24 ) & 0xFF);
        Log.d(TAG, "intToIP: result: " + ret);
        return ret;
    }
    public static InetAddress intToInetAddress(int n) {
        byte[] ret = new byte[4];
        ret[0] = (byte)((n >> 0  ) & 0xFF);
        ret[1] = (byte)((n >> 8  ) & 0xFF);
        ret[2] = (byte)((n >> 16 ) & 0xFF);
        ret[3] = (byte)((n >> 24 ) & 0xFF);
        
        Log.d(TAG, "intToInetAddress: bytes:");
        for (int i = 0; i < ret.length; i++) {
            // print unsigned values
            Log.d(TAG, "        addr[" + i + "] = " + (ret[i] & 0xff));
        }
        InetAddress addr = null;
        try {
            addr = InetAddress.getByAddress(ret);
            Log.d(TAG, "intToInetAddress: address: " + addr.getHostAddress());
        } catch (UnknownHostException e) {
            // this means "illegal ip address length", which shouldn't happen.
            assert false;
        }
        return addr;
    }
    
    public static int ipBytesToInt(byte[] addrBytes) {
        assert addrBytes.length == 4;
        int ret = 0;
        Log.d(TAG, "ipBytesToInt: bytes:");
        for (int i = 0; i<addrBytes.length; i++) {
            // print unsigned value
            Log.d(TAG, "        addr[" + i + "] = " + (addrBytes[i] & 0xff));
            int shift = 8 * i; // 0, 8, 16, 24
            ret = ret | ((addrBytes[i] & 0xff) << shift);
        }
        return ret;
    }
    
    public static int ipStringToInt(String ipAddr) {
        Log.d(TAG, "ipStringToInt: address: " + ipAddr);
        String[] blocks = ipAddr.split(".");
        assert blocks.length == 4;
        byte[] addrBytes = new byte[4];
        for (int i = 0; i < 4; i++) {
            Log.d(TAG, "        str[" + i + "] = \"" + blocks[i] + "\"");
            addrBytes[i] = Byte.parseByte(blocks[i]);
        }
        return ipBytesToInt(addrBytes);
    }
    
    public static int inetAddressToInt(InetAddress addr) {
        byte [] addrBytes = addr.getAddress();
        Log.d(TAG, "inetAddressToInt: address: " + addr.getHostAddress());
        return ipBytesToInt(addrBytes);
    }
    
    public static NetworkInterface getCellularIface(WifiInfo wifiInfo) 
        throws SocketException {
        for (Enumeration e = NetworkInterface.getNetworkInterfaces();
             e.hasMoreElements(); ) {
            NetworkInterface iface = (NetworkInterface) e.nextElement();
            InetAddress addr = getIfaceIpAddr(iface);

            if (addr != null &&
                (wifiInfo == null || 
                 !addr.equals(intToInetAddress(wifiInfo.getIpAddress())))) {
                return iface;
            }
        }
        return null;
    }
    
    public static InetAddress getIfaceIpAddr(NetworkInterface iface) {
        for (Enumeration ip = iface.getInetAddresses(); 
             ip.hasMoreElements(); ) {
            InetAddress addr = (InetAddress) ip.nextElement();
            if (!addr.isLoopbackAddress()) {
                return addr;
            }
        } 
        return null;
    }
    
    /**
     * Returns field (fieldNum) from the line of the routing output 
     *  that matches regex, or null if there is none.
     */
    private static String getWifiRoutingEntry(String tableName, String regex, 
                                              int fieldNum) {
        String cmds[] = "ip route show dev tiwlan0 table (table)".split(" ");
        cmds[6] = tableName;
        
        String gateway = null;
        
        try {
            Process p = runShellCommand(cmds);
            InputStream in = p.getInputStream();
            InputStreamReader inRdr = new InputStreamReader(in);
            BufferedReader reader = new BufferedReader(inRdr, 1024);
            
            String line = null;
            while ( (line = reader.readLine()) != null) {
                Log.d(TAG, "Getting routing table entry: " + line);
                if (line.matches(regex)) {
                    String tokens[] = line.split(" ");
                    gateway = tokens[fieldNum];
                    break;
                }
            }
            int rc = p.waitFor();
            if (rc != 0) {
                Log.e(TAG, "Failed to get wifi entry in table " + tableName);
            }
        } catch (IOException e) {
            Log.e(TAG, "Error reading from subprocess: " + e.toString());
        } catch (InterruptedException e) {
            Log.e(TAG, "Error waiting for subprocess: " + e.toString());
        }
        return gateway;
    }
    
    /**
     * Returns a String with the IP address of the WiFi gateway,
     *  or null if there is none or it couldn't be found.
     */
    public static String getWifiGateway(String tableName) {
        // format: default via <ip>
        return getWifiRoutingEntry(tableName, ".*default via.*", 2);
    }
    
    /**
     * Returns a String with the IP address of the WiFi network route,
     *  or null if there is none or it couldn't be found.
     */
    public static String getWifiNetwork(String tableName) {
        // Match xxx.yyy.zzz.www/vv (IP network block)
        final String regex = ".*[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+/[0-9]+.*";
        return getWifiRoutingEntry(tableName, regex, 0);
    }
    
    public static Process runShellCommand(String[] cmds) throws IOException {
        StringBuffer buf = new StringBuffer("Running shell command: ");
        for (String cmd : cmds) {
            buf.append(cmd).append(" ");
        }
        Log.d(TAG, buf.toString());
        return Runtime.getRuntime().exec(cmds);
    }
    
    public static void logProcessOutput(Process p) throws IOException {
        InputStream in = p.getInputStream();
        InputStreamReader rdr = new InputStreamReader(in);
        BufferedReader reader = new BufferedReader(rdr, 1024);
        String line = null;
        while ( (line = reader.readLine()) != null) {
            Log.e(TAG, "   " + line);
        }
    }
    
    public static void modifyWifiGateway(String op, String gateway, 
                                         String table) {
        String cmds[] = new String[8];
        cmds[0] = "ip";
        cmds[1] = "route";
        cmds[2] = op;
        cmds[3] = "default";
        cmds[4] = "via";
        cmds[5] = gateway; 
        cmds[6] = "table";
        cmds[7] = table;
        try {
            Process p = runShellCommand(cmds);
            int rc = p.waitFor();
            if (rc != 0) {
                Log.e(TAG, "Failed to " + op + " custom gateway:");
                logProcessOutput(p);
            }
        } catch (IOException e) {
            Log.e(TAG, "Error reading from subprocess: " + e.toString());
        } catch (InterruptedException e) {
            Log.e(TAG, "Error waiting for subprocess: " + e.toString());
        }
    }
    
    public static void modifyWifiNetwork(String op, String network, 
                                         String table) {
        String cmds[] = new String[8];
        cmds[0] = "ip";
        cmds[1] = "route";
        cmds[2] = op;
        cmds[3] = network;
        cmds[4] = "dev";
        cmds[5] = "tiwlan0"; 
        cmds[6] = "table";
        cmds[7] = table;
        
        try {
            Process p = runShellCommand(cmds);
            int rc = p.waitFor();
            if (rc != 0) {
                Log.e(TAG, "Failed to " + op + " wifi network:");
                logProcessOutput(p);
            }
        } catch (IOException e) {
            Log.e(TAG, "Error reading from subprocess: " + e.toString());
        } catch (InterruptedException e) {
            Log.e(TAG, "Error waiting for subprocess: " + e.toString());
        }
    }
    
    public static void modifyWifiRoutingRules(String op, String ipAddr) {
        String cmds[] = new String[7];
        cmds[0] = "ip";
        cmds[1] = "rule";
        cmds[2] = op;
        cmds[3] = "from";
        cmds[4] = ipAddr;
        cmds[5] = "table";
        cmds[6] = "g1custom";
        
        Process p = null;
        try {
            p = runShellCommand(cmds);
            int rc = p.waitFor();
            if (rc != 0) {
                Log.e(TAG, "Failed to " + op + " routing rules:");
                logProcessOutput(p);
            }
            
            // bidirectional routing rule
            cmds[3] = "to";
            p = runShellCommand(cmds);
            rc = p.waitFor();
            if (rc != 0) {
                Log.e(TAG, "Failed to " + op + " routing rules:");
                logProcessOutput(p);
            }
        } catch (IOException e) {
            Log.e(TAG, "Error reading from subprocess: " + e.toString());
        } catch (InterruptedException e) {
            Log.e(TAG, "Error waiting for subprocess: " + e.toString());
        }
    }
};
