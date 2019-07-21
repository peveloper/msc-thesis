#!/bin/bash


mkdir plots

#speedup=10.0
tls=5075
http=520

for file in ../log/*; do
    filename="${file##*/}"
    awk '/^ADU/ && $5 ~ /^45*/ {if(c++>0 && $4=="<1" && $6>100000){sum+=$6;time_diff=$2-_n; cum_time+=time_diff; printf("%.11f\t%s\n", cum_time, $6)};{_n=$2}}' $file >> ./$filename.dat
    #awk '/^ADU/ && $5 ~ /^45*/ {if(c++>0 && $4=="<1" && $6>100000){cum_time+=4; printf("%.11f\t%s\n", cum_time, $6)}}' $file >> ./$filename.dat
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

    segments=$(wc -l $record | cut -d ' ' -f1)
    #rec_time=$(bc <<< "5")
    #overhead=$(($tls+$http))
    echo $rec_time

    awk -v speedup="${speedup}" '{if(c++>10) {diff+=$1-_n; printf("%.5f\t%.5f\n", diff, $2)}; {_n=$1}}' $record > bytespec.tmp
    measured_bitrate=$(awk '{sum+=($2-overhead)}END{printf("%d", ((sum / NR) * 8) / 4 / 1e3)}' $record)
    #measured_bitrate=$(awk -v elapsed="${rec_time}" '{sum+=$1}END{printf("%.5f", (sum * 1e-9) / (elapsed * 0.0075)* 1e3)}' bytespec.tmp)
    #awk '{time+=$1; size+=$2}{if(time > 4) print time, size; time=$1me}' bytespec.tmp
    printf "%d\t%.3f\n" $tp $measured_bitrate >> $filename.bl
    rm -rf bytespec.tmp
    printf "%d\t%d\t" $id $measured_bitrate >> db.dat
    awk '{ printf("%d,", $2)}END{printf("%d\n", $2)}' $record >> db.dat
done

ids=($(ls | grep -e '^[0-9]*_[0-9]*.dat$' | cut -d '_' -f1 | sort -u))

for title in "${ids[@]}"; do

    echo $title

    files=$(ls *.dat.bl | grep $title)
    fingerprints=$(ls *.dat | grep $title | sort -k2,2n -t '_') 
    bitrate_ladder_data=$title.dat.bl

    cat $files >> $bitrate_ladder_data
    sort -n -o $bitrate_ladder_data $bitrate_ladder_data

    gnuplot -e "files='${fingerprints}'" movieplot
    gnuplot -e "file='${bitrate_ladder_data}'" bitrate_ladder
    #gnuplot -e "files='${fingerprints}'" ladder 

    rm -rf $files

done
