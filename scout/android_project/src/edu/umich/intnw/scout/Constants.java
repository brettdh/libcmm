package edu.umich.intnw.scout;

import android.net.ConnectivityManager;

public class Constants {
    public static final int NET_TYPE_WIFI = 1;
    public static final int NET_TYPE_THREEG = 2;
    
    public static int netTypeFromAndroidType(int type) {
        if (type == ConnectivityManager.TYPE_WIFI) {
            return NET_TYPE_WIFI;
        } else if (type == ConnectivityManager.TYPE_MOBILE) {
            return NET_TYPE_THREEG;
        }
        
        return 0;
    }
    
}
