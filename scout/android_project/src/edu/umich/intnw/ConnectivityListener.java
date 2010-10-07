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
import java.util.Date;
import java.util.Collections;
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
    private Map<Integer, NetUpdate> ifaces = 
        Collections.synchronizedMap(new HashMap<Integer, NetUpdate>());
    
    public ConnectivityListener(ConnScoutService service) {
        mScoutService = service;
        
        NetUpdate wifiNetwork = null;
        NetUpdate cellularNetwork = null;
        
        WifiManager wifi = 
            (WifiManager) mScoutService.getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifi.getConnectionInfo();
        if (wifiInfo != null) {
            wifiNetwork = new NetUpdate(intToIp(wifiInfo.getIpAddress()));
            wifiNetwork.type = ConnectivityManager.TYPE_WIFI;
            wifiNetwork.connected = true;
        }
        
        try {
            NetworkInterface cellularIface = getCellularIface(wifiInfo);
            if (cellularIface != null) {
                InetAddress addr = getIfaceIpAddr(cellularIface);
                if (addr != null) {
                    cellularNetwork = new NetUpdate(addr.getHostAddress());
                    cellularNetwork.type = ConnectivityManager.TYPE_MOBILE;
                    cellularNetwork.connected = true;
                }
            }
        } catch (SocketException e) {
            Log.e(TAG, "failed to get cellular IP address: " + e.toString());
        }
            
        ifaces.put(ConnectivityManager.TYPE_WIFI, wifiNetwork);
        ifaces.put(ConnectivityManager.TYPE_MOBILE, cellularNetwork);
        
        if (wifiNetwork != null) {
            setCustomGateway(wifiNetwork.ipAddr);
        }
        
        for (int type : ifaces.keySet()) {
            NetUpdate network = ifaces.get(type);
            if (network != null) {
                mScoutService.updateNetwork(network.ipAddr, 
                                            network.bw_down_Bps,
                                            network.bw_up_Bps, 
                                            network.rtt_ms, true);
                mScoutService.logUpdate(network.ipAddr, type, true);
            }
        }
    }
    
    public void cleanup() {
        NetUpdate wifi = ifaces.get(ConnectivityManager.TYPE_WIFI);
        if (wifi != null) {
            removeCustomGateway(wifi.ipAddr, true);
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
    
    private static int ipBytesToInt(byte[] addrBytes) {
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
    
    private static int ipStringToInt(String ipAddr) {
        Log.d(TAG, "ipStringToInt: address: " + ipAddr);
        String[] blocks = ipAddr.split(".");
        assert blocks.length == 4;
        byte[] addrBytes = new byte[4];
        for (int i = 0; i < 4; i++) {
            addrBytes[i] = Byte.parseByte(blocks[i]);
        }
        return ipBytesToInt(addrBytes);
    }
    
    private static int inetAddressToInt(InetAddress addr) {
        byte [] addrBytes = addr.getAddress();
        Log.d(TAG, "inetAddressToInt: address: " + addr.getHostAddress());
        return ipBytesToInt(addrBytes);
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
                    Log.e(TAG, "Weird... got wifi connection intent but " +
                          "WifiManager doesn't have connection info");
                    throw new NetworkStatusException();
                }
            } else {
                NetUpdate wifiNet = ifaces.get(networkInfo.getType());
                if (wifiNet == null) {
                    Log.e(TAG, "Weird... got wifi disconnection intent but " +
                          "I don't have the wifi net info");
                    throw new NetworkStatusException();
                }
                return ipStringToInt(wifiNet.ipAddr);
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
                
                NetUpdate cellular = ifaces.get(networkInfo.getType());
                if (cellular == null) {
                    Log.e(TAG, "Weird... got cellular disconnection intent but " +
                          "I don't have the wifi net info");
                    throw new NetworkStatusException();
                }
                return ipStringToInt(cellular.ipAddr);
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
    
    public static final String NETWORK_MEASUREMENT_RESULT = 
        "edu.umich.intnw.scout.NetworkMeasurementResult";
    private static final String NETWORK_STATS_EXTRA = 
        "edu.umich.intnw.scout.NetworkStatsExtra";
    
    private class MeasurementThread extends Thread {
        private ConnectivityListener mListener;
        private Map<Integer, NetUpdate> networks;
        
        public MeasurementThread(ConnectivityListener listener, 
                                 Map<Integer, NetUpdate> nets) {
            mListener = listener;
            networks = nets;
        }
        
        public void run() {
            for (int netType : networks.keySet()) {
                NetUpdate network = networks.get(netType);
                //collect measurements
                NetworkTest test = new NetworkTest(network.ipAddr);
                try {
                    test.runTests();
                    
                    //broadcast result to listener
                    network.timestamp = new Date();
                    network.bw_down_Bps = test.bw_down_Bps;
                    network.bw_up_Bps = test.bw_up_Bps;
                    network.rtt_ms = test.rtt_ms;
                    
                    Intent intent = new Intent(NETWORK_MEASUREMENT_RESULT);
                    intent.putExtra(NETWORK_STATS_EXTRA, network);
                    mListener.mScoutService.sendBroadcast(intent);
                } catch (NetworkTest.NetworkTestException e) {
                    Log.d(TAG, "Network measurement failed: " + e.toString());
                    ConnScoutService srv = mListener.mScoutService;
                    srv.reportMeasurementFailure(netType, network.ipAddr);
                }
            }
            mListener.measurementThread = null;
            mListener.mScoutService.measurementDone(null);
        }
    }
    
    private Thread measurementThread = null;
    
    public void measureNetworks() {
        if (measurementThread == null) {
            final Map<Integer, NetUpdate> networks
                = new HashMap<Integer, NetUpdate>();
            for (int type : ifaces.keySet()) {
                NetUpdate network = ifaces.get(type);
                networks.put(type, (NetUpdate) network.clone());
            }
            
            measurementThread = new MeasurementThread(this, networks);
            measurementThread.start();
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
        
        String action = intent.getAction();
        if (action.equals(ConnectivityManager.CONNECTIVITY_ACTION)) {
            NetworkInfo networkInfo = 
                (NetworkInfo) extras.get(ConnectivityManager.EXTRA_NETWORK_INFO);
            try {
                int bw_down_Bps = 0;
                int bw_up_Bps = 0;
                int rtt_ms = 0;
                
                int curAddr = getIpAddr(networkInfo);
                String ipAddr = intToIp(curAddr);
                
                NetUpdate prevNet = ifaces.get(networkInfo.getType());
                if (prevNet != null && !prevNet.ipAddr.equals(ipAddr)) {
                    // put down the old network's IP addr
                    mScoutService.updateNetwork(prevNet.ipAddr, 0, 0, 0, true);
                }
                
                if (networkInfo.isConnected()) {
                    NetUpdate network = prevNet;
                    if (network == null) {
                        network = new NetUpdate(ipAddr);
                        network.type = networkInfo.getType();
                        network.connected = true;
                    }
                    if (prevNet != null && prevNet.ipAddr.equals(ipAddr)) {
                        // preserve existing stats
                    } else {
                        // optimistic fake estimate while we wait for 
                        //  real measurements
                        network.bw_down_Bps = 1250000;
                        network.bw_up_Bps = 1250000;
                        network.rtt_ms = 1;
                    }
                    bw_down_Bps = network.bw_down_Bps;
                    bw_up_Bps = network.bw_up_Bps;
                    rtt_ms = network.rtt_ms;
                    
                    network.ipAddr = ipAddr;
                    ifaces.put(networkInfo.getType(), network);
                    if (networkInfo.getType() ==
                        ConnectivityManager.TYPE_WIFI) {
                        setCustomGateway(ipAddr);
                    }
                } else {
                    ifaces.put(networkInfo.getType(), null);
                    if (networkInfo.getType() ==
                        ConnectivityManager.TYPE_WIFI) {
                        removeCustomGateway(ipAddr, false);
                    }
                }
                
                // TODO: real network measurements here
                mScoutService.updateNetwork(ipAddr, 
                                            bw_down_Bps, bw_up_Bps, rtt_ms,
                                            !networkInfo.isConnected());
                mScoutService.logUpdate(ipAddr, networkInfo);
                
            } catch (NetworkStatusException e) {
                // ignore; already logged
            } catch (SocketException e) {
                Log.e(TAG, "failed to get IP address: " + e.toString());
            }
        } else if (action.equals(NETWORK_MEASUREMENT_RESULT)) {
            NetUpdate network = (NetUpdate) extras.get(NETWORK_STATS_EXTRA);
            Log.d(TAG, "Got network measurement result for " + network.ipAddr);
            
            NetUpdate targetNet = ifaces.get(network.type);
            if (targetNet != null) {
                Log.d(TAG, "  Result: " + network.statsString());
                targetNet.bw_down_Bps = network.bw_down_Bps;
                targetNet.bw_up_Bps = network.bw_up_Bps;
                targetNet.rtt_ms = network.rtt_ms;
                
                mScoutService.updateNetwork(network.ipAddr, 
                                            network.bw_down_Bps,
                                            network.bw_up_Bps,
                                            network.rtt_ms,
                                            false);
                mScoutService.logUpdate(network);
            }
        } else {
            // unknown action; ignore (shouldn't happen)
        }
    }
};
