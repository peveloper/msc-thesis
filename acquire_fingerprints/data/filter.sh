#!/bin/bash


mkdir plots

global=""
c=0
speedup=1.8

read_elapsed_time () {
    awk '/^ADU/ {if(c++==0) {d1=$1}} END{printf("%.2f%s\t", $1-d1, " secs")}' $1
}

for file in ../log/*; do
    filename="${file##*/}"
    #awk '/^ADU/ {if(c++>0 && $4=="<1" && $6>200000){time_diff=$2-_n; cum_time+=time_diff; printf("%.7f\t%s\n", cum_time, $6)};{_n=$2}}' $file >> ./$filename.dat
    #read_elapsed_time $filename.dat
    #gnuplot -e "file='${filename}.dat'" singleplot
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

    elapsed_time=$(awk 'END{print $1}' $record)
    awk -v speedup="${speedup}" '{if(c++>0) {diff=($1*speedup)-_n; printf("%.5f\n", $2 / diff)}; {_n=($1*speedup)}}' $record > bytespec.tmp
    awk -v elapsed="${elapsed_time}" '{sum+=$1}END{printf("avg Bitrate: %.5f bytes/s\n", sum / elapsed)}' bytespec.tmp
    rm -rf bytespec.tmp

    filename="${record##*/}"
    echo $filename

    id=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
    tp=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')

    echo $id $tp >> db.dat
    cat $record >> db.dat
done

#gnuplot -e "files='${global}'" globalplot

titles=$(cat titles)
#tps=$(cat tps)

for title in "${titles[@]}"; do
    # For each title get all the fingerprints at different resolutions
    awk -v title="${title}" '{if($1==title){flag=1;next}}flag' db.dat
done
