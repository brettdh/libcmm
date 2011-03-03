package edu.umich.intnw.scout;

import android.os.Parcelable;
import android.os.Parcel;
import java.util.Date;
import edu.umich.intnw.scout.Utilities;

public final class NetUpdate implements Parcelable, Cloneable {
    public Date timestamp;
    public String ipAddr;
    public int type;
    public boolean connected;
    public int bw_down_Bps;
    public int bw_up_Bps;
    public int rtt_ms;
    
    private final static int BW_DOWN_NO_STATS = 1250000;
    private final static int BW_UP_NO_STATS = 1250000;
    private final static int RTT_NO_STATS = 1;
    
    public NetUpdate(String ip) {
        timestamp = new Date();
        ipAddr = new String(ip);
        setNoStats();
    }
    
    public void setNoStats() {
        bw_down_Bps = BW_DOWN_NO_STATS;
        bw_up_Bps = BW_UP_NO_STATS;
        rtt_ms = RTT_NO_STATS;
    }
    
    public Object clone() {
        try {
            NetUpdate newUpdate = (NetUpdate) super.clone();
            if (timestamp != null) {
                newUpdate.timestamp = (Date) timestamp.clone();
            }
            if (ipAddr != null) {
                newUpdate.ipAddr = new String(ipAddr);
            }
            return newUpdate;
        } catch (CloneNotSupportedException e) {
            // shouldn't ever happen
            assert false;
            return null;
        }
    }
    
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
        dest.writeInt(bw_down_Bps);
        dest.writeInt(bw_up_Bps);
        dest.writeInt(rtt_ms);
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
        bw_down_Bps = in.readInt();
        bw_up_Bps = in.readInt();
        rtt_ms = in.readInt();
    }
    
    public String toString() {
        StringBuilder str = new StringBuilder();
        str.append(Utilities.formatTimestamp(timestamp))
           .append(" ")
           .append(ipAddr)
           .append(" ")
           .append(connected ? "up" : "down");
        if (hasStats()) {
            str.append(", ").append(statsString());
        }
        return str.toString();
    }
    
    public boolean hasStats() {
        if (!connected) {
            return false;
        } else if (bw_down_Bps == 0 && bw_up_Bps == 0 && rtt_ms == 0) {
            return false;
        } else if (bw_down_Bps == BW_DOWN_NO_STATS && 
                   bw_up_Bps == BW_UP_NO_STATS && 
                   rtt_ms == RTT_NO_STATS) {
            return false;
        } else {
            return true;
        }
    }
    
    public String statsString() {
        StringBuilder str = new StringBuilder();
        str.append(bw_down_Bps)
           .append(" / ")
           .append(bw_up_Bps)
           .append(" / ")
           .append(rtt_ms);
        return str.toString();
    }
};
