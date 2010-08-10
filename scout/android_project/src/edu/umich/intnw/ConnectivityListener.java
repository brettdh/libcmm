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

import edu.umich.intnw.scout.ConnScoutService;

public class ConnectivityListener extends BroadcastReceiver {
    private static String TAG = ConnectivityListener.class.getName();
    
    private ConnScoutService mScoutService;
    // XXX: may have more than one WiFi IP eventually.
    private int wifiIpAddr;
    private int cellularIpAddr;
    
    public ConnectivityListener(ConnScoutService service) {
        mScoutService = service;
        wifiIpAddr = 0;
        cellularIpAddr = 0;
    }
    
    private static String intToIp(int i) {
        return ((i >>  0 ) & 0xFF) + "." +
               ((i >>  8 ) & 0xFF) + "." +
               ((i >> 16 ) & 0xFF) + "." +
               ((i >> 24 ) & 0xFF);
    }
    private static InetAddress intToInetAddress(int n) {
        byte[] ret = new byte[4];
        ret[0] = (byte)((n >> 0  ) & 0xFF);
        ret[1] = (byte)((n >> 8  ) & 0xFF);
        ret[2] = (byte)((n >> 16 ) & 0xFF);
        ret[3] = (byte)((n >> 24 ) & 0xFF);
        
        Log.d(TAG, "intToInetAddress: bytes:");
        for (int i = 0; i < ret.length; i++) {
            Log.d(TAG, "        addr[" + i + "] = " + ret[i]);
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
            Log.d(TAG, "        addr[" + i + "] = " + addrBytes[i]);
            int shift = 8 * i; // 0, 8, 16, 24
            ret += (addrBytes[i] << shift);
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
                    wifiIpAddr = wifiInfo.getIpAddress();
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
                if (wifiIpAddr == 0) {
                    Log.e(TAG, "Weird... got wifi disconnect intent but" +
                          "I haven't seen a wifi connection intent");
                    throw new NetworkStatusException();
                } else {
                    int ret = wifiIpAddr;
                    wifiIpAddr = 0;
                    return ret;
                }
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
                
                cellularIpAddr = 
                    inetAddressToInt(getIfaceIpAddr(cellular_iface));
                return cellularIpAddr;
            } else {
                if (cellular_iface != null) {
                    Log.e(TAG, "Weird... got cellular disconnect intent " +
                          "but still have the cellular iface");
                    throw new NetworkStatusException();
                }
                
                if (cellularIpAddr == 0) {
                    Log.e(TAG, "Weird... got cellular disconnect intent but" +
                          "I haven't seen a cellular connection intent");
                    throw new NetworkStatusException();
                } else {
                    int ret = cellularIpAddr;
                    cellularIpAddr = 0;
                    return ret;
                }
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
            String ipAddr = intToIp(getIpAddr(networkInfo));
            mScoutService.updateNetwork(ipAddr, 1250000, 1250000, 1, 
                                        !networkInfo.isConnected());
            mScoutService.logUpdate(ipAddr, !networkInfo.isConnected());
            
        } catch (NetworkStatusException e) {
            // ignore; already logged
        } catch (SocketException e) {
            Log.e(TAG, "failed to get IP address: " + e.toString());
        }
    }
};
