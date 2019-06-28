#!/bin/bash


mkdir plots

speedup=1.0
minutes_recorded=2

for file in ../log/*; do
    filename="${file##*/}"
    awk '/^ADU/ && $5 ~ /^45*/ {if(c++>0 && $4=="<1" && $6>200000){time_diff=$2-_n; cum_time+=time_diff; printf("%.11f\t%s\n", cum_time, $6)};{_n=$2}}' $file >> ./$filename.dat
    gnuplot -e "file='${filename}.dat'" singleplot
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
    measured_bitrate=$(awk -v elapsed="${minutes_recorded}" '{sum+=$1}END{printf("%.5f", (sum * 1e-9) / (elapsed * 0.0075)* 1e3)}' bytespec.tmp)
    printf "%d\t%.3f\n" $tp $measured_bitrate >> $filename.bl
    rm -rf bytespec.tmp
    printf "%d\t%d\n" $id $tp >> db.dat
    awk '{print $2}' $record >> db.dat
done

ids=($(ls | grep -e '^[0-9]*_[0-9]*.dat$' | cut -d '.' -f1))

for title in "${ids[@]}"; do

    bitrate_ladder_data=$title.dat.bl
    fingerprints=$title.dat

    sort -n -o $bitrate_ladder_data $bitrate_ladder_data

    gnuplot -e "files='${fingerprints}'" movieplot
    gnuplot -e "file='${bitrate_ladder_data}'" bitrate_ladder

done
