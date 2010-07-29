package edu.umich.intnw;

import android.os.Parcelable;
import android.os.Parcel;
import java.util.Date;
import java.util.Calendar;
import java.util.GregorianCalendar;

public final class NetUpdate implements Parcelable {
    public Date timestamp;
    public String ipAddr;
    public boolean down;
    
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
        
        // why is there no writeBoolean?
        boolean[] oneBool = new boolean[1];
        oneBool[0] = down;
        dest.writeBooleanArray(oneBool);
    }
    
    public NetUpdate() {}
    public NetUpdate(Parcel in) {
        readFromParcel(in);
    }
    
    public void readFromParcel(Parcel in) {
        timestamp = new Date(in.readLong());
        ipAddr = in.readString();
        boolean[] oneBool = new boolean[1];
        in.readBooleanArray(oneBool);
        down = oneBool[0];
    }
    
    public String toString() {
        StringBuilder str = new StringBuilder();
        Calendar cal = new GregorianCalendar();
        cal.setTime(timestamp);
        str.append("[")
           .append(cal.get(Calendar.HOUR_OF_DAY))
           .append(":")
           .append(cal.get(Calendar.MINUTE))
           .append(":")
           .append(cal.get(Calendar.SECOND))
           .append("] ")
           .append(ipAddr)
           .append(" ")
           .append(down ? "down" : "up");
        return str.toString();
    }
};
