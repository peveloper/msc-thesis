#!/bin/bash


mkdir plots

global=""
c=0
speedup=1.0

#read_elapsed_time () {
    #awk '/^ADU/ {if(c++==0) {d1=$1}} END{printf("%.2f%s\t", $1-d1, " secs")}' $1
#}

for file in ../log/*; do
    filename="${file##*/}"
    awk '/^ADU/||$5 ~ /^45*/ {if(c++>0 && $4=="<1" && $6>20000){time_diff=$2-_n; cum_time+=time_diff; printf("%.7f\t%s\n", cum_time, $6)};{_n=$2}}' $file >> ./$filename.dat
    #read_elapsed_time $filename.dat
    gnuplot -e "file='${filename}.dat'" singleplot
    ((c++))

    if (( $c > 1 ))
    then
        global="${global} ${filename}"
    else
        global="${filename}"
    fi
done

for record in *.dat; do
    if [ $record = "db.dat" ]
    then
        break
    fi

    filename="${record##*/}"
    elapsed_time=$(awk 'END{print $1}' $record)
    id=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
    tp=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')
    echo $filename

    awk -v speedup="${speedup}" '{if(c++>0) {diff+=$1-_n; printf("%.5f\n", $2)}; {_n=$1}}' $record > bytespec.tmp
    measured_bitrate=$(awk -v elapsed="${elapsed_time}" '{sum+=$1}END{printf("%.5f", sum / 120)}' bytespec.tmp)
    printf "%d\t%.3f\n" $tp $measured_bitrate >> $filename.bl
    rm -rf bytespec.tmp

    printf "%d\t%d\n" $id $tp >> db.dat
    cat $record >> db.dat
done

gnuplot -e "files='${global}'" globalplot

titles=$'\r\n' GLOBIGNORE='*' command eval  'ids=($(cat titles))'
#tps=$(cat tps)

for title in "${ids[@]}"; do

    files=$(ls *.dat.bl | grep $title)

    bitrate_ladder_data=$title.dat.bl

    cat $files >> $bitrate_ladder_data

    sort -n -o $bitrate_ladder_data $bitrate_ladder_data

    rm -rf $files

    fingerprints=$(ls *.dat | grep $title)
    gnuplot -e "files='${fingerprints}'" movieplot

    #tps=$(echo $files | grep -E -o '[0-9]+\.' | sed 's/\.//g')

    #echo "$files" "$tps"

    gnuplot -e "file='${bitrate_ladder_data}'" bitrate_ladder


    # For each title get all the fingerprints at different resolutions
    #elapsed_time=$(awk -v title="${title}" '{if($1==title){flag=1;next}{if!(title in movies}flag' db.dat | awk '{print $1}')
    #bytes=$(awk -v title="${title}" '{if($1==title){flag=1;next}}flag' db.dat | awk '{print $2}')

    #echo "$elapsed_time"
    #echo "$bytes"
    #echo "${tps[0]}"
    #echo "${tps[2]}"
done
