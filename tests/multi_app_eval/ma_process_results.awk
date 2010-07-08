BEGIN {
    split("", vanilla_fg_response_times);
    split("", vanilla_bg_total_throughputs);
    split("", intnw_fg_response_time);
    split("", intnw_bg_total_throughputs);

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
    split("", vanilla_fg_sender_nums);
    split("", vanilla_bg_sender_nums);
    split("", intnw_fg_sender_nums);
    split("", intnw_bg_sender_nums);

    total_send_time = 0;
    send_count = 0;
    sender_num = 0;
    sender_count = 0;
}

# Worker PID 12345 - intnw foreground sender results
/Worker PID [0-9]+ - .+ .+ sender results/ {
    pid = $3;
    type = $5; # 'intnw' or 'vanilla'
    label = $6; # 'foreground' or 'background'
    
    #if it's not the first time...
    if (current_type != "" && sender_num == 0 || sender_num == pid) {
        # add to running average - depends on whether I'm FG or BG
        # TODO: pick up here.  write down what figures we need
        #       and then figure out how to compute them.
        # XXX: the array management and other code is really ugly.
        #  Might be better overall to rewrite in Python;
        #  the simplicity of AWK's text parsing doesn't appear
        #  to be worth the headaches.
        if (current_type == "vanilla") {
            if (current_label == "foreground") {
                vanilla_fg_response_times[run_num] += (total_send_time / send_count);
            } else if (current_label == "background") {
                vanilla_bg_throughputs[run_num] += ((send_count * current_chunksize) / total_send_time);
            }
        } else if (current_type == "intnw") {
            if (current_label == "foreground") { 
                intnw_fg_response_times[run_num] += (total_send_time / send_count);
            } else if (current_label == "background") {
                intnw_bg_throughputs[run_num] += ((send_count * current_chunksize) / total_send_time);
            }
        }
    }

    if (sender_num != pid) {
        if (current_type == "vanilla") {
            if (current_label == "foreground") {
                vanilla_fg_response_times[run_num] /= sender_count;
            } else if (current_label == "background") {
                vanilla_bg_response_times[run_num] /= sender_count;
            }
        } else if (current_type == "intnw") {
            if (current_label == "foreground") { 
                intnw_fg_response_times[run_num] /= sender_count;
            } else if (current_label == "background") {
                intnw_bg_response_times[run_num] /= sender_count;
            }
        }
        
        sender_count = 0;
    } else {
        sender_count += 1;
    }
    sender_num = pid;
    total_send_time = 0;
    send_count = 0;

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

    total_send_time += response_time;
    send_count += 1;
}

# print everything as it is processed
{
    print;
}

function print_summary(results_array)
{
    cur_run = 0;
    cur_sender = 0;
    for (run in results_array) {
        split(run, indices, SUBSEP);
        if (
            printf("Run %d:\n", );
            cur_run = indices[i];
            sender_num = indices[i+1];
            
        }
    }
    
}

END {
    # print summary
    
}