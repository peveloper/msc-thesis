#!/bin/bash

# Start the Server

DB_PATH=$1
WIN_SIZE=$2
KEY_SIZE=$3
KEY_MODE=$4
INPUT_FILE=$5

cd server && ./runServer.bash $DB_PATH $WIN_SIZE $KEY_SIZE $KEY_MODE> /dev/null 2>&1 & 

#echo 'Database setting up...'
sleep 5

# Start the Client

#echo 'Identifying movie...'
cd client && cat $INPUT_FILE | grep ADU | awk '$6 > 100000' | cut -d " " -f2,3,4,5,6 | python preprocessor.py | python detector.py 127.0.0.1 10007 $WIN_SIZE

pkill -f '.*java.*' > /dev/null 2>&1 &
