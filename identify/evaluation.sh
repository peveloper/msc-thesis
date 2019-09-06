#!/bin/bash

DB_PATH=$1
INPUT_FILE=$2

default() {
    DB_PATH=$1
    INPUT_FILE=$2
    WIN_SIZE=$3
    KEY_SIZE=$4
    KEY_MODE=$5

    FILE_NAME="$(basename -- $INPUT_FILE)"
    id=$(echo $FILE_NAME | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
    

    echo $FILE_NAME
    # Get the ID of the captured movie
    # Query the KD tree 
    result=$(./test.sh $DB_PATH $WIN_SIZE $KEY_SIZE $KEY_MODE $INPUT_FILE)
    length=$(echo $result | wc -w)

    echo $result

    if [[ $length == 5 ]]; then 
        returned_id=$(echo $result | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
        returned_tp=$(echo $result | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')
        returned_bitrate=$(echo $result | awk '{print $2}')
        returned_idx_1=$(echo $result | awk '{print $3}')
        returned_idx_2=$(echo $result | awk '{print $4}')
        elapsed_time=$(echo $result | awk '{print $5}')

        name="${returned_id}_${returned_tp}"
        end=$((returned_idx_1 + WIN_SIZE))
        end_capture=$((returned_idx_2 + WIN_SIZE))
        
        db_idxs=$(seq -s, $returned_idx_1 1 $end)
        match=$([[ "$id" == "$returned_id" ]] && echo 1 || echo 0)

        #If match we plot the two window starting at indexes idx_1 idx_2
        if [[ $match == 1 ]];then
            db_path="../acquire_fingerprints/data/db.dat"
            db_segments=$(cat $db_path | grep $name | cut -d '   ' -f3 | cut -d ',' -f$db_idxs)
            capture_segments=$(cat $INPUT_FILE | awk -v idx="${returned_idx_2}" end="${end_capture}" '{if(NR >= idx && NR <= end) {printf("%s,", $6)}}')

        fi

    fi

}


get_factors() {
    num=$1
    fs=""
    for (( i=2; i<=$1; i++ ));do
        while [ $((num%$i)) == 0 ];do
            if [ -z "$fs" ]
            then
                fs="{$i"
            else
                fs="${fs},$i"
            fi
            num=$((num/$i))
        done
    done
    fs="${fs}}"
    echo $fs
}


gen_experiments() {
combinations=$(eval "echo $KEY_SIZES+$WIN_SIZES" | awk '{for(i=1; i <= NF; i++) {split($i, factors, "+");printf("%d\n", factors[1]*factors[2]); printf("%d\n", factors[1]*(factors[2]-1))}}' | xargs -n1 -P2 bash -c 'get_factors "$@"' _ | sort | uniq | awk '{n=split($1, factors, ","); if(n>1) print $1}')

    for item in $combinations; do

        item=$(echo $item | sed -e 's/[{}]//g')

        IFS=',' read -ra factors <<< "$item"

        WIN_SIZE=1
        for i in "${factors[@]}"
        do
            if [[ $WIN_SIZES != *$i* ]]; then
                WIN_SIZE=-1
                break
            fi

            WIN_SIZE=$((WIN_SIZE * i))
        done

        if [[ $WIN_SIZE == -1 ]];
        then
            continue
        fi

        echo $item $WIN_SIZE

    done
}

export -f get_factors
export -f gen_experiments
export -f default

if [[ $# == 5 ]]; then
    WIN_SIZE=$3
    KEY_SIZE=$4
    KEY_MODE=$5
    
    default $DB_PATH $INPUT_FILE $WIN_SIZE $KEY_SIZE $KEY_MODE
else

    NR=$(cat $INPUT_FILE | wc -l)
    KEY_SIZES="{1,2,3,4,5,6,7,8,9}"
    KEY_MODES="{0,1}"

    WIN_SIZES=$(get_factors $NR)

    export WIN_SIZES=$WIN_SIZES
    export KEY_SIZES=$KEY_SIZES

    gen_experiments
fi
