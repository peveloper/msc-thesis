#!/bin/bash

adu_quiet_time=500
export adu_quiet_time=$adu_quiet_time
adu_plots="adu_plots/"
adu_dir="adu"

mkdir -p $adu_dir
mkdir -p $adu_plots


get_adus() {
    file=$2
    adu_file="adu/$file"
    tp=$(echo $file | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2 * 1e3}')
    echo $tp
    if [ $1 == 0 ];
    then
        ../tools/adudump/adudump ../log/$file -l 192.168.0.157/24 -q $adu_quiet_time > $adu_file
    fi
    
    declare -a local_ports=$(cut -d ' ' -f3 $adu_file | sort | uniq | cut -d '.' -f5)
    src_ips=$(cat ../har/$file.har | cut -f1 | cut -d '/' -f3 | sort | uniq | xargs -l1 host | cut -d ' ' -f4)
    awk -v out="${adu_file}" -v ips="${src_ips}" 'BEGIN{split(ips, ip_list, " "); for (i in ip_list) ip_values[ip_list[i]] = "";}{if (($1 ~ /^ADU/ || $1 ~ /^INC/) && $4=="<1" && substr($5, 0, length($5) - 4) in ip_values && $6>100000) print > out;}' $adu_file 
    awk '{printf("%d\t%s\n",NR,$6);}' $adu_file > ./$file.dat
}


build_fingerprint_db() {
    record=$1
    id=$(echo $record | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
    tp=$(echo $record | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2 * 1e3}')
    segments=$(wc -l $record | cut -d ' ' -f1)
    measured_bitrate=$(awk '{sum+=$2}END{printf("%d", ((sum / NR) * 8) / 4 )}' $record)
    printf "%d\t%.3f\n" $tp $measured_bitrate > $record.bl
    br=$((measured_bitrate / 1000))
    tp=$((tp / 1000)) 
    name="${id}_${tp}"
    awk -v name="${name}" -v br="${br}" 'BEGIN{printf("%s\t%d\t", name, br);}{ printf("%d,", $2)}END{printf("\n")}' $record
}

ids=($(ls | grep -e '^[0-9]*_[0-9]*.dat$' | cut -d '_' -f1 | sort -u))
export ids=$ids

gen_plots() {
    for title in "${ids[@]}"; do
        files=$(ls *.dat.bl | grep $title)
        fingerprints=$(ls *.dat | grep $title | sort -k2,2n -t '_') 
        #echo $fingerprints
        bitrate_ladder_data=$title.dat.bl
        cat $files >> $bitrate_ladder_data
        sort -n -o $bitrate_ladder_data $bitrate_ladder_data

        gnuplot -e "files='${fingerprints}'" movieplot
        gnuplot -c bitrate_ladder $adu_plots $bitrate_ladder_data

        rm -rf $files
    done
}


export -f get_adus
export -f build_fingerprint_db
export -f gen_plots
export adu_plots=$adu_plots

#ls ../log/ | parallel get_adus 1
echo "DONE"
#ls | grep -e '^[0-9]*_[0-9]*.dat$' | parallel build_fingerprint_db > db.dat
echo "DID"
gen_plots
