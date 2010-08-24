package edu.umich.intnw.scout;

import android.os.Parcelable;
import android.os.Parcel;
import android.net.NetworkInfo;
import java.util.Date;
import java.util.Calendar;
import java.util.GregorianCalendar;
import edu.umich.intnw.scout.Utilities;

public final class NetUpdate implements Parcelable {
    public Date timestamp;
    public String ipAddr;
    public int type;
    public boolean connected;
    
    public static final Parcelable.Creator<NetUpdate> CREATOR = 
        new Parcelable.Creator<NetUpdate>() {
            public NetUpdate createFromParcel(Parcel in) {
                return new NetUpdate(in);
            }
            
            public NetUpdate[] newArray(int size) {
                return new NetUpdate[size];
            }
        };
    
    public int describeContents() {
        return 0;
    }
    public void writeToParcel(Parcel dest, int flags) {
        dest.writeLong(timestamp.getTime());
        dest.writeString(ipAddr);
        dest.writeInt(type);
        dest.writeInt(connected ? 1 : 0);
    }
    
    public NetUpdate() {}
    public NetUpdate(Parcel in) {
        readFromParcel(in);
    }
    
    public void readFromParcel(Parcel in) {
        timestamp = new Date(in.readLong());
        ipAddr = in.readString();
        type = in.readInt();
        connected = (in.readInt() == 1);
    }
    
    public String toString() {
        StringBuilder str = new StringBuilder();
        str.append(Utilities.formatTimestamp(timestamp))
           .append(" ")
           .append(ipAddr)
           .append(" ")
           .append(connected ? "up" : "down");
        return str.toString();
    }
};
