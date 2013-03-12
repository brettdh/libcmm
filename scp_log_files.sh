#!/bin/sh

usage() {
    echo "Usage: $1 <hostname:/path/to/dir> </path/to/local/dir>"
    exit 1
}

if [ $# != 2 ]; then
    usage $0
fi

FILES=" intnw.log trace_replayer.log timing.log instruments.log replayer_server.log"
FILES="$FILES client_error_distributions.txt server_error_distributions.txt"

REMOTE_DIR=$1
LOCAL_DIR=$2
REMOTE_FILES=${FILES// / $REMOTE_DIR\/}

if [ -d $LOCAL_DIR ]; then
    scp $REMOTE_FILES $LOCAL_DIR
else
    echo "Error: $LOCAL_DIR doesn't exist"
    exit 1
fi
