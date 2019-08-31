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
    extension="${file##*.}"

    cp $file $file.tcp

    sudo ../tools/adudump/adudump -q 1000 $file -l 192.168.0.157/24 > tmp

    mv tmp $file

    declare -a local_ports=$(cut -d ' ' -f3 $file | sort | uniq | cut -d '.' -f5)

    for port in ${local_ports[@]}; do
        awk -v port="${port}" '{if(match($3, port "$") && $4=="<1") {sum+=$6; print;}}END{print sum}' $file
    done

    src_ips=$(cat ../har/$filename.har | cut -f1 | cut -d '/' -f3 | sort | uniq | xargs -l1 host | cut -d ' ' -f4)
    
    echo $src_ips

    awk -v ips="${src_ips}" 'BEGIN{split(ips, ip_list, " "); for (i in ip_list) ip_values[ip_list[i]] = "";}{if (($1 ~ /^ADU/ || $1 ~ /^INC/) && $4=="<1" && substr($5, 0, 12) in ip_values && $6>10000) {c+=1; printf("%d\t%s\n",c,$6);}}' $file > ./$filename.dat

    mv $file.tcp $file
    
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

    awk -v speedup="${speedup}" '{if(c++>10) {diff+=$1-_n; printf("%.5f\t%.5f\n", diff, $2)}; {_n=$1}}' $record > bytespec.tmp
    measured_bitrate=$(awk '{sum+=$2}END{printf("%d", ((sum / NR) * 8) / 4 )}' $record)
    printf "%d\t%.3f\n" $tp $measured_bitrate >> $filename.bl
    rm -rf bytespec.tmp
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
