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
        
        for (int type : ifaces.keySet()) {
            int ipAddr = ifaces.get(type);
            if (ipAddr != 0) {
                String ip = intToIp(ipAddr);
                mScoutService.updateNetwork(ip, 1250000, 1250000, 1, true);
                mScoutService.logUpdate(ip, type, true);
            }
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
            } else {
                ifaces.put(networkInfo.getType(), 0);
                // TODO: remove my custom default gateway
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
