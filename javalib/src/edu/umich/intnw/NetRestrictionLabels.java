package edu.umich.intnw;

//XXX: import these from native intnw library
public class NetRestrictionLabels {
    private static final int NET_RESTRICTION_SHIFT = 16;
    
    public static final int WIFI_ONLY = 1 << NET_RESTRICTION_SHIFT;
    public static final int THREEG_ONLY = 2 << NET_RESTRICTION_SHIFT;
    public static final int ALL_NET_RESTRICTION_LABELS = WIFI_ONLY | THREEG_ONLY;
}
