package edu.umich.intnw;

import java.util.Date;
import java.util.Calendar;
import java.util.GregorianCalendar;

public class Utilities {
    /**
     * Append the formatted timestamp to str, as [hh:mm:ss]
     */
    public static String formatTimestamp(Date timestamp) {
        Calendar cal = new GregorianCalendar();
        cal.setTime(timestamp);
        StringBuilder str = new StringBuilder();
        str.append("[")
           .append(cal.get(Calendar.HOUR_OF_DAY))
           .append(":")
           .append(cal.get(Calendar.MINUTE))
           .append(":")
           .append(cal.get(Calendar.SECOND))
           .append("]");
        return str.toString();
    }
};