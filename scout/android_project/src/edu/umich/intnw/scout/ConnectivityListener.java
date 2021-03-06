package edu.umich.intnw.scout;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.util.Log;
import android.os.Bundle;
import android.net.wifi.WifiManager;
import android.net.wifi.WifiInfo;

import java.net.NetworkInterface;
import java.net.InetAddress;
import java.util.Date;
import java.util.Collections;
import java.net.SocketException;
import java.util.Map;
import java.util.HashMap;

import edu.umich.intnw.scout.ConnScoutService;
import static edu.umich.intnw.scout.Utilities.*;

public class ConnectivityListener extends BroadcastReceiver {
    private static String TAG = ConnectivityListener.class.getName();
    
    private ConnScoutService mScoutService;
    // XXX: may have more than one WiFi IP eventually.
    private Map<Integer, NetUpdate> ifaces = 
        Collections.synchronizedMap(new HashMap<Integer, NetUpdate>());
    
    private NetUpdate lastMobileStats;
    private String lastSSID;
    private String lastBSSID;
    
    private void setWifiInfo(WifiInfo wifiInfo) {
        if (wifiInfo == null) {
            lastSSID = null;
            lastBSSID = null;
        } else {
            lastSSID = wifiInfo.getSSID();
            lastBSSID = wifiInfo.getBSSID();
        }
    }

    public ConnectivityListener(ConnScoutService service) {
        mScoutService = service;
        
        NetUpdate wifiNetwork = null;
        NetUpdate cellularNetwork = null;
        
        lastMobileStats = new NetUpdate();
        
        WifiManager wifi = 
            (WifiManager) mScoutService.getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifi.getConnectionInfo();
        if (wifiInfo != null && wifiInfo.getIpAddress() != 0) {
            wifiNetwork = new NetUpdate(intToIp(wifiInfo.getIpAddress()));
            wifiNetwork.type = ConnectivityManager.TYPE_WIFI;
            wifiNetwork.connected = true;
            setWifiInfo(wifiInfo);
            
            BreadcrumbsNetworkStats stats = 
                BreadcrumbsNetworkStats.lookup(wifiInfo.getSSID(), 
                                               wifiInfo.getBSSID());
            if (stats != null) {
                wifiNetwork.setStats(stats);
            }
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
        
        for (int type : ifaces.keySet()) {
            NetUpdate network = ifaces.get(type);
            if (network != null) {
                Log.d(TAG, "Adding network: " + network.ipAddr);
                mScoutService.updateNetwork(network.ipAddr, 
                                            network.bw_down_Bps,
                                            network.bw_up_Bps, 
                                            network.rtt_ms, false,
                                            Constants.netTypeFromAndroidType(type));
                mScoutService.logUpdate(network.ipAddr, type, true);
            }
        }
    }
    
    private class NetworkStatusException extends Exception {};
    
    /* getIpAddr
     * only call once per delivered intent.
     * assumption 1: intents will arrive as connect-disconnect pairs,
     *   with no connect-connect or disconnect-disconnect pairs.
     * assumption 2: when a connect intent arrives, the NetworkInterface
     *   for the relevant network will exist. (need to verify)
     * if either is violated, a NetworkStatusException will be thrown.
     */
    private String getIpAddr(NetworkInfo networkInfo)
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
                    
                    return intToIp(wifiIpAddr);
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
                return wifiNet.ipAddr;
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
                
                return getIfaceIpAddr(cellular_iface).getHostAddress();
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
                return cellular.ipAddr;
            }
        }
    }
    
    public static final String NETWORK_MEASUREMENT_RESULT = 
        "edu.umich.intnw.scout.NetworkMeasurementResult";
    private static final String NETWORK_STATS_EXTRA = 
        "edu.umich.intnw.scout.NetworkStatsExtra";
    
    public static final String ACTION_START_MEASUREMENT = 
        "edu.umich.intnw.scout.StartMeasurement";
    
    public static final String INTNW_SCOUT_WIFI_AVAILABLE = 
        "edu.umich.intnw.scout.IntNWScoutWifiAvailable";
    
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
            synchronized (mListener) {
                mListener.measurementThread = null;
            }
            mListener.mScoutService.measurementDone(null);
        }
    }
    
    private Thread measurementThread = null;

    public boolean measurementInProgress() {
        synchronized (this) {
            return measurementThread != null;
        }
    }
    
    public void measureNetworks() {
        synchronized (this) {
            if (measurementThread == null) {
                final Map<Integer, NetUpdate> networks
                    = new HashMap<Integer, NetUpdate>();
                for (int type : ifaces.keySet()) {
                    NetUpdate network = ifaces.get(type);
                    if (network != null) {
                        networks.put(type, (NetUpdate) network.clone());
                    }
                }
                
                measurementThread = new MeasurementThread(this, networks);
                measurementThread.start();
            }
        }

        Intent startNotification = new Intent(ConnScoutService.BROADCAST_MEASUREMENT_STARTED);
        mScoutService.sendBroadcast(startNotification);
    }
    
    private boolean networkHasChanged(NetUpdate prevNet, NetworkInfo networkInfo)
        throws SocketException, NetworkStatusException {
        boolean wifi_change = false;
        if (networkInfo.getType() == ConnectivityManager.TYPE_WIFI) {
            WifiManager wifi = 
                (WifiManager) mScoutService.getSystemService(Context.WIFI_SERVICE);
            WifiInfo wifiInfo = wifi.getConnectionInfo();
            if (wifiInfo != null) {
                wifi_change = ((lastSSID == null || lastBSSID == null) || 
                               (!lastSSID.equals(wifiInfo.getSSID()) || 
                                !lastBSSID.equals(wifiInfo.getBSSID())));
            }
        }
        String ipAddr = getIpAddr(networkInfo);
        boolean ip_change = !prevNet.ipAddr.equals(ipAddr);
        
        return wifi_change || ip_change;
    }
    
    @Override
    public void onReceive(Context context, Intent intent) {
        Log.d(TAG, "Got event");
        Log.d(TAG, "Context: " + context.toString());
        Log.d(TAG, "Intent: " + intent.toString());
        Bundle extras = intent.getExtras();
        if (extras != null) {
            for (String key : extras.keySet()) {
                Log.d(TAG, key + "=>" + extras.get(key));
            }
        }
        
        String action = intent.getAction();
        if (action.equals(ConnectivityManager.CONNECTIVITY_ACTION)) {
            NetworkInfo networkInfo = 
                (NetworkInfo) extras.get(ConnectivityManager.EXTRA_NETWORK_INFO);
            if (networkInfo.getType() != ConnectivityManager.TYPE_MOBILE &&
                networkInfo.getType() != ConnectivityManager.TYPE_WIFI) {
                // ignore unknown network types.  this includes weird
                //  duplicate types like TYPE_MOBILE_SUPL.
                return;
            }
            
            try {
                int bw_down_Bps = 0;
                int bw_up_Bps = 0;
                int rtt_ms = 0;
                
                String ipAddr = getIpAddr(networkInfo);
                
                NetUpdate prevNet = ifaces.get(networkInfo.getType());
                if (prevNet != null && networkHasChanged(prevNet, networkInfo)) {
                    // put down the old network's IP addr
                    Log.d(TAG, String.format("Clearing network (IP changed from %s to %s)",
                                             prevNet.ipAddr, ipAddr));
                    mScoutService.updateNetwork(prevNet.ipAddr, 0, 0, 0, true,
                                                Constants.netTypeFromAndroidType(networkInfo.getType()));
                }
                
                boolean ignore = false;
                NetUpdate network;
                if (networkInfo.isConnected()) {
                    network = prevNet;
                    if (network == null) {
                        network = new NetUpdate(ipAddr);
                        network.type = networkInfo.getType();
                        network.connected = true;
                    }
                    if (prevNet != null && !networkHasChanged(prevNet, networkInfo)) {
                        // preserve existing stats; don't send this 'update' to IntNW apps
                        // just ignore it
                        ignore = true;
                        Log.d(TAG, String.format("Ignoring network update for %s (no change)", ipAddr));
                    } else if (networkInfo.getType() == ConnectivityManager.TYPE_MOBILE) {
                        Log.d(TAG, String.format("Reusing old mobile network stats for %s", ipAddr));
                        network.setStats(lastMobileStats);
                    } else {
                        BreadcrumbsNetworkStats bcStats = null;
                        if (networkInfo.getType() == ConnectivityManager.TYPE_WIFI) {
                            bcStats = BreadcrumbsNetworkStats.lookupCurrentAP(mScoutService);

                            WifiManager wifi = 
                                (WifiManager) mScoutService.getSystemService(Context.WIFI_SERVICE);
                            WifiInfo wifiInfo = wifi.getConnectionInfo();
                            setWifiInfo(wifiInfo);
                        }
                        if (bcStats != null) {
                            Log.d(TAG, String.format("Got wifi network stats for %s from breadcrumbs db", ipAddr));
                            network.setStats(bcStats);
                        } else {
                            // optimistic fake estimate while we wait for 
                            //  real measurements
                            Log.d(TAG, String.format("Set fake wifi network stats for %s (no db entry)", ipAddr));
                            network.setNoStats();
                        }
                    }
                    bw_down_Bps = network.bw_down_Bps;
                    bw_up_Bps = network.bw_up_Bps;
                    rtt_ms = network.rtt_ms;
                    
                    network.ipAddr = ipAddr;
                    ifaces.put(networkInfo.getType(), network);

                } else {
                    Log.d(TAG, String.format("Interface no longer connected: %s", ipAddr));
                    ifaces.put(networkInfo.getType(), null);
                    network = new NetUpdate(ipAddr);
                    network.type = networkInfo.getType();
                    network.connected = false;
                    
                    if (network.type == ConnectivityManager.TYPE_WIFI) {
                        setWifiInfo(null);
                    }
                }

                if (!ignore) {
                    // TODO: real network measurements here
                    mScoutService.updateNetwork(ipAddr, 
                                                bw_down_Bps, bw_up_Bps, rtt_ms,
                                                !networkInfo.isConnected(),
                                                Constants.netTypeFromAndroidType(networkInfo.getType()));
                    mScoutService.logUpdate(network);
                    if (network.type == ConnectivityManager.TYPE_WIFI &&
                        networkInfo.isConnected()) {
                        mScoutService.sendBroadcast(new Intent(INTNW_SCOUT_WIFI_AVAILABLE));
                    }
                }
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
                
                int intnwNetType = Constants.netTypeFromAndroidType(network.type);
                mScoutService.updateNetwork(network.ipAddr, 
                                            network.bw_down_Bps,
                                            network.bw_up_Bps,
                                            network.rtt_ms,
                                            false, intnwNetType);
                mScoutService.logUpdate(network);
                
                if (network.type == ConnectivityManager.TYPE_MOBILE) {
                    lastMobileStats.setStats(network);
                }
            }
        } else if (action.equals(ACTION_START_MEASUREMENT)) {
            Log.d(TAG, "Got start-measurement intent; starting measurement");
            measureNetworks();
        } else {
            // unknown action; ignore (shouldn't happen)
        }
    }
};
