#!/bin/bash

: '
plot accuracy in terms of:
# matches, #mismatches, #collisions (*)
'
generate_accuracy_plot() {

    local capture_id=$1
    local capture_tp=$2
    local win_size=$3
    local key_size=$4
    local key_mode=$5
    local key_delta=$6
    local matches=$7
    local collisions=$8
    local mismatches=$9
    local plot_filename=acc_${capture_id}_${capture_tp}_${key_mode}_${key_delta}_${threshold}

    # generate accuracy_plot_file
    if [[ ! -f $plot_filename ]]; then
        printf "Window\tMatches\tCollisions\tMismatches\n" > $plot_filename
    fi


    #TODO(?) take the percentage 

    x="${win_size}(${key_size})"
    y_1=$matches
    y_2=$collisions
    y_3=$mismatches

    paste <(echo "$x") <(echo "$y_1") <(echo "$y_2") <(echo "$y_3") >> $plot_filename

    echo $plot_filename
}
export -f generate_accuracy_plot

plot_accuracy() {
    gnuplot -c accuracy "$@"
    rm -rf $1
}
export -f plot_accuracy

: '
(*) collision
if correct matching windows from same movie but at different bandwidths.
'

default() {

    : '
    Query the KD-Tree for matching windows, reporting the following stats in
    matches folder

    - matches: # windows of movies with same ID as video capture
    - mismatches: # windows uncorrectly matching (different movie ID)
    - collisions: # different bandwidths correctly matching video capture
    '

    local db_path=$1
    local input_file=$2
    local win_size=$3
    local key_size=$4
    local key_mode=$5
    local key_delta=$6
    local threshold=$7
    local plot_file

    local matches=0
    local collisions=0
    local mismatches=0
    local collisions_array=()

    # get id and enforced bandwidth of the current capture
    local capture_filename="$(basename -- $input_file)"
    local capture_id=$(echo $capture_filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
    local capture_tp=$(echo $capture_filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')

    # query the kd tree 
    ./exec_query.sh $db_path $input_file $win_size $key_size $key_mode $key_delta $threshold

    local match_filename="${capture_filename}_${win_size}_${key_size}_${key_mode}_${key_delta}"

    if [ -f matches/$match_filename ];
    then
        tps=""
        local i=0

        # check for matches, count the # of collisions and the number of mismatches
        while IFS= read -r line; do 
            if (( i == 0)); then
                ((i++))
                continue
            fi

            # get candidate match info
            id=$(echo $line | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
            tp=$(echo $line | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')
            bitrate=$(echo $line | awk '{print $2}')
            index=$(echo $line | awk '{print $3}')
            capture_index=$(echo $line | awk '{print $4}')
            key=$(echo $line | awk -v len="${key_size}" '{for(i=5; i < 5+len; i++) {print $i}}')
            capture_key=$(echo $line | awk -v len="${key_size}" '{for(i=5+len; i < 5+(2*len); i++) {print $i}}')
            correlation=$(echo $line | awk -v len="${key_size}" '{print $(5+(2*len))}')
            file_name="${id}_${tp}"
            end=$((index + win_size - 1))
            capture_end=$((capture_index + win_size - 1))
            match=$([[ "$id" == "$capture_id" ]] && echo 1 || echo 0)


            # check if match
            if [[ $match == 1 ]]; then
                idxs=$(seq -s, $index 1 $end)
                #local plot_filename="match_${i}_${id}_${tp}_${capture_id}_${capture_tp}_${win_size}_${key_size}_${key_mode}_${key_delta}"

                ((i++))

                # generate plot_data
                x=$(seq 1 $win_size)
                y_1=$(cat $db_path | grep $file_name | cut -d '	' -f3 | cut -d ',' -f$idxs | tr ',' '\n')
                y_2=$(cat $input_file | awk -v idx="${capture_index}" -v end="${capture_end}" '{if(nr >= idx && nr <= end) {print $6}}')

                #TODO FIX yerr here

                #paste <(echo "$x") <(echo "$y_1") <(echo "$y_2") > plot_file
                #paste <(awk '{print sqrt(($2-$3)**2)}' plot_file) > plo
                #exit 1
                #gnuplot -c window $file_name $capture_filename $index $capture_index $win_size $key_size $key_mode $key_delta ${plot_file}
                if (($matches > 0));
                then
                    if [[ $tps != *"${tp}"* ]]; then
                        tps="${tps}_${tp}" 
                    fi
                else
                    tps="${tp}"
                fi
                ((matches++))
            else
                # mismatch 
                ((mismatches++))
            fi

        done < matches/$match_filename

        IFS='_' read -ra collisions_array <<< "$tps"

        if (( ${#collisions_array[@]} > 1))
        then
            collisions=${#collisions_array[@]} 
        fi

        my_result=$(generate_accuracy_plot $capture_id $capture_tp $win_size $key_size $key_mode $key_delta $matches $collisions $mismatches)
       
        echo $my_result
    fi

}

get_factors() {

    : '
    Compute factors of a given number
    '
    n=$1

    for i in $(seq 1 $n)
    do
        [ $(expr $n / $i \* $i) == $n ] && echo $i
    done

}

export -f get_factors
export -f default

main() {

    db_path=$1
    input_file=$2

    if [[ $# == 7 ]]; then

        : '
        Execute a single test run with the supplied parameters.
        '
        win_size=$3
        key_size=$4
        key_mode=$5
        key_delta=$6
        threshold=$7

        default $db_path $input_file $win_size $key_size $key_mode $key_delta $threshold plot_file

    elif [[ $# == 6 ]]; then

        : '
        Execute multiple test runs, in which window_size ranges from 1 to
        #video-segments in the capture.  key_sizes are computed as the factors of
        current window_size, or simply window_size + 1 if window_size is prime.
        '

        key_mode=$3
        key_delta=$4
        max_win=$5
        threshold=$6
        capture_filename="$(basename -- $input_file)"

        if [ -z "$max_win" ];
        then
            nr=$(cat $input_file | wc -l)
        else
            nr=$max_win
        fi


        capture_id=$(echo $capture_filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
        capture_tp=$(echo $capture_filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')

        echo -e "Analysing ID ${capture_id} recorded at ${capture_tp}\n"

        for((win_size=2; win_size < nr; win_size++))
        do
            key_sizes=$(get_factors $win_size | sort | uniq)
            echo $key_sizes
            number_of_factors=$(wc -w <<< "$key_sizes")

            if (( number_of_factors > 1 )); then
                for key_size in $key_sizes; do
                    ((++key_size))
                    echo "WIN_SIZE ${win_size} KEY_SIZE ${key_size}"
                    if [[ -z ${plot_file} ]];then
                        plot_file=$(default $db_path $input_file $win_size $key_size $key_mode $key_delta $threshold)
                    else
                        default $db_path $input_file $win_size $key_size $key_mode $key_delta $threshold
                    fi
                done
            else
                # prime number of segments in window
                key_size=$((win_size + 1))
                echo "WIN_SIZE ${win_size} KEY_SIZE ${key_size}"
                if [[ -z ${plot_file} ]];then
                    plot_file=$(default $db_path $input_file $win_size $key_size $key_mode $key_delta $threshold)
                else
                    default $db_path $input_file $win_size $key_size $key_mode $key_delta $threshold
                fi
            fi
        done

        plot_accuracy $plot_file $capture_filename $key_mode $key_delta $threshold
    fi
}
export -f main

evaluate_dataset() {
    main "$@" 
}
export -f evaluate_dataset

evaluate_dataset "$@"
#capture_dir="../acquire_fingerprints/capture_log/"
#ls $capture_dir | parallel --dry-run evaluate_dataset $1 {} $2 $3 $4 $5
