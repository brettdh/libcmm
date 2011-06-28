package edu.umich.intnw.scout;

import android.content.Context;
import android.database.Cursor;
import android.database.CursorIndexOutOfBoundsException;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteException;
import android.net.ConnectivityManager;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.util.Log;

class BreadcrumbsNetworkStats {
    private static final String dbFilename = 
        "/sdcard/sl4a/scripts/breadcrumbs/db/breadcrumbs.db";
    private static final String TAG = BreadcrumbsNetworkStats.class.getName();
    public int bw_down;
    public int bw_up;
    public int rtt_ms;
    
    public static BreadcrumbsNetworkStats lookupCurrentAP(Context ctx) {
        WifiManager wifi = (WifiManager)
            ctx.getSystemService(Context.WIFI_SERVICE);
        WifiInfo wifiInfo = wifi.getConnectionInfo();
        if (wifiInfo != null) {
            String essid = wifiInfo.getSSID();
            String bssid = wifiInfo.getBSSID();
            if (essid != null && bssid != null) {
                return lookup(essid, bssid);
            }
        }
        return null;
    }
    
    public static BreadcrumbsNetworkStats lookup(String essid, String bssid) {
        SQLiteDatabase db = null;
        Cursor c = null;
        try {
            db = SQLiteDatabase.openDatabase(
                    dbFilename, null, SQLiteDatabase.OPEN_READONLY
            );
            if (db == null) {
                Log.e(TAG, "Couldn't open db file " + dbFilename);
                return null;
            }
            
            String[] cols = {"dbw", "ubw", "rtt"};
            String[] whereArgs = {essid, bssid};
            c = db.query("aps", cols, "essid=? AND bssid=?", whereArgs, 
                         null, null, null);
            if (c.getCount() != 1 || !c.moveToFirst()) {
                Log.e(TAG, String.format("No stats available for AP %s (%s)",
                        essid, bssid));
                return null;
            }
            
            BreadcrumbsNetworkStats stats = new BreadcrumbsNetworkStats();
            try {
                stats.bw_down = (int) c.getFloat(0);
                stats.bw_up = (int) c.getFloat(1);
                stats.rtt_ms = (int) c.getFloat(2);
            } catch (CursorIndexOutOfBoundsException e) {
                Log.e(TAG, "Failed to get AP data: " + e.toString());
                stats = null;
            }
            
            return stats;
        } catch (SQLiteException e) {
            Log.e(TAG, "Database error: " + e.getMessage());
            return null;
        } finally {
            if (c != null) {
                c.close();
            }
            if (db != null) {
                db.close();
            }
        }
    }
}