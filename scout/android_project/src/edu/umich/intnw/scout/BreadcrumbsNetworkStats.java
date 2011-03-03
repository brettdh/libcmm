package edu.umich.intnw.scout;

import android.content.Context;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;
import android.database.sqlite.SQLiteDatabase.CursorFactory;
import android.database.sqlite.SQLiteOpenHelper;
import android.util.Log;

class BreadcrumbsNetworkStats {
    private static final String dbFilename = 
        "/sdcard/sl4a/scripts/breadcrumbs/db/breadcrumbs.db";
    private static final String TAG = BreadcrumbsNetworkStats.class.getName();
    public int bw_down;
    public int bw_up;
    public int rtt_ms;
    
    static class MyOpenHelper extends SQLiteOpenHelper {
        public MyOpenHelper(Context context) {
            super(context, dbFilename, null, 3);
            // TODO Auto-generated constructor stub
        }

        @Override
        public void onCreate(SQLiteDatabase db) {
            // TODO Auto-generated method stub
            
        }

        @Override
        public void onUpgrade(SQLiteDatabase db, int oldVersion, int newVersion) {
            // TODO Auto-generated method stub
            
        }
    }
    
    static BreadcrumbsNetworkStats lookup(Context ctx, String essid, String bssid) {
        // XXX: BROKEN.
        return null;
        /*
        MyOpenHelper helper = new MyOpenHelper(ctx);
        SQLiteDatabase db = helper.getReadableDatabase();
        if (db == null) {
            Log.e(TAG, "Couldn't open db file " + dbFilename);
            return null;
        }
        
        String[] cols = {"dbw", "ubw", "rtt"};
        String[] whereArgs = {essid, bssid};
        Cursor c = db.query("aps", cols, "essid=? AND bssid=?", whereArgs, 
                            null, null, null);
        if (c.getCount() == 0) {
            Log.e(TAG, String.format("No stats available for AP %s (%s)",
                                     essid, bssid));
            return null;
        }
        
        BreadcrumbsNetworkStats stats = new BreadcrumbsNetworkStats();
        stats.bw_down = (int) c.getFloat(0);
        stats.bw_up = (int) c.getFloat(1);
        stats.rtt_ms = (int) c.getFloat(2);
        c.close();
        return stats;
        */
    }
}