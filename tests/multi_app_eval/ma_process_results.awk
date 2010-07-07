BEGIN {
    split("", vanilla_avg_fg_response_times);
    split("", vanilla_avg_bg_total_throughputs);
    split("", intnw_avg_fg_response_time);
    split("", intnw_avg_bg_total_throughputs);

    split("", vanilla_avg_fg_chunksizes);
    split("", vanilla_avg_bg_chunksizes);
    split("", intnw_avg_fg_chunksizes);
    split("", intnw_avg_bg_chunksizes);

    current_type = "";
    current_label = "";
}

/Results for run [0-9]+/ {
    run_num = $4;

    # initialize data structures

    total_send_times = 0;
    send_count = 0;
}

# Worker PID 12345 - intnw foreground sender results
/Worker PID [0-9]+ - .+ .+ sender results/ {
    pid = $3;
    type = $5; # 'intnw' or 'vanilla'
    label = $6; # 'foreground' or 'background'
    
    #if it's not the first time...
    if (current_type != "") {
        # add to running average - depends on whether I'm FG or BG
        # TODO: pick up here.  write down what figures we need
        #       and then figure out how to compute them.
        if (current_type == "vanilla") {
            if (current_label == "foreground") {
                #vanilla_avg_fg_response_times[
            } else if (current_label == "background") {
                
            }
        } else if (current_type == "intnw") {
            if (current_label == "foreground") {
                
            } else if (current_label == "background") {
                
            }
        }
    }

    # initialize data structures
    current_type = type;
    current_label = label;

        
    if (type != "vanilla" && type != "intnw") {
        printf("SAW UNKNOWN TYPE %s\n", type);
    }
    if (label != "foreground" && label != "background") {
        printf("SAW UNKNOWN LABEL %s\n", label);
    }
}

/Chunksize: [0-9]+/ {
    chunksize = $2;
    current_chunksize = chunksize;
}

# Timestamp            Response time (sec)     Throughput (bytes/sec)
# 1278534838.454732          35.580977               7367.532376
/[0-9]+\.[0-9+]+ +[0-9]+\.[0-9+]+ +[0-9]+\.[0-9+]+/ {
    timestamp = $1;
    response_time = $2;

    
    
}

# print everything as it is processed
{
    print;
}

END {
    # print summary
    
}