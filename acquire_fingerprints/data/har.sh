#!/bin/bash

#mkdir plots

tls=5075
http=520
overhead=$(($tls+$http))
overhead=0

for file in ../har/*; do
    filename="${file##*/}"

    echo $filename

    cat $file |  cut -d ' ' -f234 > tmp
    # process har
    awk -v overhead="${overhead}" '{if($1 > 100000) printf("%d\t%d\n", NR, $1-overhead)}' tmp > ./$filename
    
done

for record in *.har; do
    filename="${record##*/}"
    id=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
    tp=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')

    echo $filename
    echo $id
    echo $tp

    #segments=$(wc -l $record | cut -d ' ' -f1)

    measured_bitrate=$(awk '{sum+=$2}END{printf("%d", ((sum / NR) * 8) / 4 )}' $record)
    printf "%d\t%.3f\n" $tp $measured_bitrate >> $filename.bl
    rm -rf bytespec.tmp
    br=$((measured_bitrate / 1000))
    printf "%d\t%d\t" $id $br >> db.dat
    awk '{ printf("%d,", $2)}END{printf("%d\n", $2)}' $record >> db.dat
done

ids=($(ls | grep -e '^[0-9]*_[0-9]*.har$' | cut -d '_' -f1 | sort -u))

for title in "${ids[@]}"; do

    echo $title

    files=$(ls *.har.bl | grep $title)
    fingerprints=$(ls *.har | grep $title | sort -k2,2n -t '_') 
    bitrate_ladder_data=$title.har.bl

    cat $files >> $bitrate_ladder_data
    sort -n -o $bitrate_ladder_data $bitrate_ladder_data

    echo $fingerprints
    gnuplot -e "files='${fingerprints}'" movieplot
    gnuplot -e "file='${bitrate_ladder_data}'" bitrate_ladder

    rm -rf $files

done
