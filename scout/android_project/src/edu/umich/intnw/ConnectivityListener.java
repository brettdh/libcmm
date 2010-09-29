package edu.umich.intnw.scout;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.util.Log;
import android.os.Bundle;
import android.net.wifi.WifiManager;
import android.net.wifi.WifiInfo;

import java.net.NetworkInterface;
import java.net.InetAddress;
import java.util.Enumeration;
import java.net.SocketException;
import java.net.UnknownHostException;
import java.util.Map;
import java.util.HashMap;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.BufferedReader;
import java.io.IOException;

import edu.umich.intnw.scout.ConnScoutService;

public class ConnectivityListener extends BroadcastReceiver {
    private static String TAG = ConnectivityListener.class.getName();
    
    private ConnScoutService mScoutService;
    // XXX: may have more than one WiFi IP eventually.
    private int wifiIpAddr;
    private int cellularIpAddr;
    private Map<Integer, Integer> ifaces = new HashMap<Integer, Integer>();
    
    public ConnectivityListener(ConnScoutService service) {
        mScoutService = service;
        
        int wifiIpAddr = 0;
        int cellularIpAddr = 0;
        
        WifiManager wifi = 
            (WifiManager) mScoutService.getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifi.getConnectionInfo();
        if (wifiInfo != null) {
            wifiIpAddr = wifiInfo.getIpAddress();
        }
        
        try {
            NetworkInterface cellularIface = getCellularIface(wifiInfo);
            if (cellularIface != null) {
                InetAddress addr = getIfaceIpAddr(cellularIface);
                if (addr != null) {
                    cellularIpAddr = inetAddressToInt(addr);
                }
            }
        } catch (SocketException e) {
            Log.e(TAG, "failed to get cellular IP address: " + e.toString());
        }
            
        ifaces.put(ConnectivityManager.TYPE_WIFI, wifiIpAddr);
        ifaces.put(ConnectivityManager.TYPE_MOBILE, cellularIpAddr);
        
        if (wifiIpAddr != 0) {
            String ipAddr = intToIp(wifiIpAddr);
            setCustomGateway(ipAddr);
        }
        
        for (int type : ifaces.keySet()) {
            int ipAddr = ifaces.get(type);
            if (ipAddr != 0) {
                String ip = intToIp(ipAddr);
                mScoutService.updateNetwork(ip, 1250000, 1250000, 1, true);
                mScoutService.logUpdate(ip, type, true);
            }
        }
    }
    
    public void cleanup() {
        int wifiIp = ifaces.get(ConnectivityManager.TYPE_WIFI);
        if (wifiIp != 0) {
            String ipAddr = intToIp(wifiIp);
            removeCustomGateway(ipAddr, true);
        }
    }
    
    private static String intToIp(int i) {
        InetAddress ip = intToInetAddress(i);
        Log.d(TAG, "intToIp: equivalent InetAddress: " + ip.getHostAddress());
        String ret = ((i >>  0 ) & 0xFF) + "." +
                     ((i >>  8 ) & 0xFF) + "." +
                     ((i >> 16 ) & 0xFF) + "." +
                     ((i >> 24 ) & 0xFF);
        Log.d(TAG, "intToIP: result: " + ret);
        return ret;
    }
    private static InetAddress intToInetAddress(int n) {
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
    
    private static int inetAddressToInt(InetAddress addr) {
        int ret = 0;
        byte [] addrBytes = addr.getAddress();
        Log.d(TAG, "inetAddressToInt: address: " + addr.getHostAddress());
        Log.d(TAG, "inetAddressToInt: bytes:");
        for (int i = 0; i<addrBytes.length; i++) {
            // print unsigned value
            Log.d(TAG, "        addr[" + i + "] = " + (addrBytes[i] & 0xff));
            int shift = 8 * i; // 0, 8, 16, 24
            ret = ret | ((addrBytes[i] & 0xff) << shift);
        }
        return ret;
    }
    
    private static NetworkInterface getCellularIface(WifiInfo wifiInfo) 
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
    
    private static InetAddress getIfaceIpAddr(NetworkInterface iface) {
        for (Enumeration ip = iface.getInetAddresses(); 
             ip.hasMoreElements(); ) {
            InetAddress addr = (InetAddress) ip.nextElement();
            if (!addr.isLoopbackAddress()) {
                return addr;
            }
        } 
        return null;
    }
    
    private class NetworkStatusException extends Exception {};
    
    /** getIpAddr
     * only call once per delivered intent.
     * side effect: stores (or unstores) the IP address
     *   that it returns.
     * assumption 1: intents will arrive as connect-disconnect pairs,
     *   with no connect-connect or disconnect-disconnect pairs.
     * assumption 2: when a connect intent arrives, the NetworkInterface
     *   for the relevant network will exist. (need to verify)
     * if either is violated, a NetworkStatusException will be thrown.
     */
    private int getIpAddr(NetworkInfo networkInfo)
        throws NetworkStatusException, SocketException {
        // XXX: may have more than one WiFi IP eventually.
        WifiManager wifi = 
            (WifiManager) mScoutService.getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifi.getConnectionInfo();
        
        if (networkInfo.getType() == ConnectivityManager.TYPE_WIFI) {
            if (networkInfo.isConnected()) {
                if (wifiInfo != null) {
                    int wifiIpAddr = wifiInfo.getIpAddress();
                    // InetAddress addr = intToInetAddress(wifiIpAddr);
                    // Log.d(TAG, "getIpAddr: returning wifi IP " +
                    //       addr.getHostAddress());
                    
                    // DEBUG: get the wifi iface by getting the only
                    //   non-loopback iface
                    // NetworkInterface wifiIface = getCellularIface(null);
                    // InetAddress debugAddr = getIfaceIpAddr(wifiIface);
                    // Log.d(TAG, "getIpAddr: wifi iface IP is " +
                    //       debugAddr.getHostAddress());
                    
                    return wifiIpAddr;
                } else {
                    Log.e(TAG, "Weird... got wifi connection intent but" +
                          "WifiManager doesn't have connection info");
                    throw new NetworkStatusException();
                }
            } else {
                return ifaces.get(networkInfo.getType());
            }
        } else { // cellular (TYPE_MOBILE)
            // first, find the IP address of the cellular interface
            NetworkInterface cellular_iface = getCellularIface(wifiInfo);
            if (networkInfo.isConnected()) {
                if (cellular_iface == null) {
                    Log.e(TAG, "Weird... got cellular connect intent " +
                          "but no cellular iface");
                    throw new NetworkStatusException();
                }
                
                return inetAddressToInt(getIfaceIpAddr(cellular_iface));
            } else {
                if (cellular_iface != null) {
                    Log.e(TAG, "Weird... got cellular disconnect intent " +
                          "but still have the cellular iface");
                    throw new NetworkStatusException();
                }
                
                return ifaces.get(networkInfo.getType());
            }
        }
    }
    
    /**
     * Returns a String with the IP address of the WiFi gateway,
     *  or null if there is none or it couldn't be found.
     */
    private String getWifiGateway(String tableName) {
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
                Log.d(TAG, "Getting gateway: " + line);
                if (line.contains("default via ")) {
                    // Format: "default via <ip>"
                    String tokens[] = line.split(" ");
                    gateway = tokens[2];
                    break;
                }
            }
            int rc = p.waitFor();
            if (rc != 0) {
                Log.e(TAG, "Failed to get wifi gateway in table " + tableName);
            }
        } catch (IOException e) {
            Log.e(TAG, "Error reading from subprocess: " + e.toString());
        } catch (InterruptedException e) {
            Log.e(TAG, "Error waiting for subprocess: " + e.toString());
        }
        return gateway;
    }
    
    private Process runShellCommand(String[] cmds) throws IOException {
        StringBuffer buf = new StringBuffer("Running shell command: ");
        for (String cmd : cmds) {
            buf.append(cmd).append(" ");
        }
        Log.d(TAG, buf.toString());
        return Runtime.getRuntime().exec(cmds);
    }
    
    private void logProcessOutput(Process p) throws IOException {
        InputStream in = p.getInputStream();
        InputStreamReader rdr = new InputStreamReader(in);
        BufferedReader reader = new BufferedReader(rdr, 1024);
        String line = null;
        while ( (line = reader.readLine()) != null) {
            Log.e(TAG, "   " + line);
        }
    }
    
    private void modifyWifiGateway(String op, String gateway, String table) {
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
    
    private void modifyWifiRoutingRules(String op, String ipAddr) {
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
    
    private void setCustomGateway(String ipAddr) {
        /*
        Steps:
        1) Get already-configured gateway address
        2) Remove system-added gateway
        3) Add new gateway in routing table 'g1custom'
        4) Add routing rules for g1custom table
        */
        Log.d(TAG, "Setting up gateway and routing rules for " + ipAddr);
        String gateway = getWifiGateway("main");
        if (gateway != null) {
            modifyWifiGateway("del", gateway, "main");
            modifyWifiGateway("add", gateway, "g1custom");
            modifyWifiRoutingRules("add", ipAddr);
        } else {
            Log.e(TAG, "Couldn't find gateway for tiwlan0 in main table");
        }
    }
    
    private void removeCustomGateway(String ipAddr, boolean restoreOld) {
        /*
        Steps:
        1) Get already-configured gateway address
        2) Remove my custom gateway (it's probably gone already)
        3) Remove routing rules for g1custom table
        */
        Log.d(TAG, "Removing custom routing setup for " + ipAddr);
        String gateway = getWifiGateway("g1custom");
        if (gateway != null) {
            modifyWifiGateway("del", gateway, "g1custom");
            if (restoreOld) {
                modifyWifiGateway("append", gateway, "main");
            }
            modifyWifiRoutingRules("del", ipAddr);
        } else {
            Log.e(TAG, "Couldn't find gateway for tiwlan0 in g1custom table");
        }
    }
    
    @Override
    public void onReceive(Context context, Intent intent) {
        Log.d(TAG, "Got event");
        Log.d(TAG, "Context: " + context.toString());
        Log.d(TAG, "Intent: " + intent.toString());
        Bundle extras = intent.getExtras();
        for (String key : extras.keySet()) {
            Log.d(TAG, key + "=>" + extras.get(key));
        }
        
        NetworkInfo networkInfo = 
            (NetworkInfo) extras.get(ConnectivityManager.EXTRA_NETWORK_INFO);
        try {
            int curAddr = getIpAddr(networkInfo);
            String ipAddr = intToIp(curAddr);
            int prevAddr = ifaces.get(networkInfo.getType());
            if (prevAddr != 0 && prevAddr != curAddr) {
                // put down the old network's IP addr
                mScoutService.updateNetwork(intToIp(prevAddr), 0, 0, 0, true);
            }
            
            if (networkInfo.isConnected()) {
                ifaces.put(networkInfo.getType(), curAddr);
                // TODO: switch out old default gateway for mine, with
                //  "g1custom" table
                if (networkInfo.getType() ==
                    ConnectivityManager.TYPE_WIFI) {
                    setCustomGateway(ipAddr);
                }
            } else {
                ifaces.put(networkInfo.getType(), 0);
                // TODO: remove my custom default gateway
                if (networkInfo.getType() ==
                    ConnectivityManager.TYPE_WIFI) {
                    removeCustomGateway(ipAddr, false);
                }
            }
            
            // TODO: real network measurements here
            mScoutService.updateNetwork(ipAddr, 1250000, 1250000, 1, 
                                        !networkInfo.isConnected());
            mScoutService.logUpdate(ipAddr, networkInfo);
            
        } catch (NetworkStatusException e) {
            // ignore; already logged
        } catch (SocketException e) {
            Log.e(TAG, "failed to get IP address: " + e.toString());
        }
    }
};
