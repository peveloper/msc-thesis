#!/bin/bash


mkdir plots

#speedup=10.0
tls=5075
http=520
overhead=$(($tls+$http))
overhead=0

c=0
for file in ../log/*; do
    filename="${file##*/}"
    #awk '/^ADU/ && $5 ~ /^45*/ {if(c++>0 && $4=="<1" && $6>10000){sum+=$6;time_diff=$2-_n; cum_time+=time_diff; printf("%.11f\t%s\n", cum_time, $6)};{_n=$2}}' $file >> ./$filename.dat
    awk '(/^ADU/ || /^INC/) && $5 ~ /^45*/ {if(c++>0 && $4=="<1" && $6>100000){cum_time+=1; printf("%s\n", $6)}}' $file > tmp
    
    awk -v overhead="${overhead}" '{printf("%d\t%d\n", NR, $1-overhead)}' tmp > ./$filename.dat

    gnuplot -e "file='${filename}.dat'" singleplot

    if (( c++ > 24 ));
    then
        break
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
    tp=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2 * 1e3}')

    echo $filename

    segments=$(wc -l $record | cut -d ' ' -f1)
    #rec_time=$(bc <<< "5")
    #overhead=$(($tls+$http))
    #echo $rec_time

    awk -v speedup="${speedup}" '{if(c++>10) {diff+=$1-_n; printf("%.5f\t%.5f\n", diff, $2)}; {_n=$1}}' $record > bytespec.tmp
    measured_bitrate=$(awk '{sum+=$2}END{printf("%d", ((sum / NR) * 8) / 4 )}' $record)
    #measured_bitrate=$(awk -v elapsed="${rec_time}" '{sum+=$1}END{printf("%.5f", (sum * 1e-9) / (elapsed * 0.0075)* 1e3)}' bytespec.tmp)
    #awk '{time+=$1; size+=$2}{if(time > 4) print time, size; time=$1me}' bytespec.tmp
    #bitrate=$(awk -v bitrate="${measured_bitrate}" '{print bitrate / 1e3}')
    printf "%d\t%.3f\n" $tp $measured_bitrate >> $filename.bl
    rm -rf bytespec.tmp
    #bitrate=$(awk -v br="${measured_bitrate}" 'END{print br}')
    br=$((measured_bitrate / 1000))
    echo $br
    printf "%d\t%d\t" $id $br >> db.dat
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

    rm -rf $files

done
