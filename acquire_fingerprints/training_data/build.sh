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
    awk '{printf("%d\t%s\n",NR,$6);}' $adu_file > ./$file.adu
}

get_hars() {
    
    mkdir -p har_plots/

    tls=5075
    http=520
    #overhead=$(($tls+$http))
    overhead=0

    for file in ../har/*; do
        filename="${file##*/}"

        cat $file |  cut  -f2 > tmp
        # process har
        awk -v overhead="${overhead}" '{if($1 > 100000) c++; printf("%d\t%d\n", c, $1-overhead)}' tmp > ./$filename

        rm -rf tmp
        
    done
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


gen_plots() {
    ext=$1
    ids=($(ls | grep -e "^[0-9]*_[0-9]*.${ext}$" | cut -d '_' -f1 | sort -u))

    echo "${ids[@]}"

    for title in "${ids[@]}"; do
        files=$(ls *.${ext}.bl | grep $title)
        fingerprints=$(ls *.${ext} | grep $title | sort -k2,2n -t '_') 
        bitrate_ladder_data=$title.$ext.bl

        cat $files >> $bitrate_ladder_data
        sort -n -o $bitrate_ladder_data $bitrate_ladder_data

        plot_dir="${ext}_plots/"

        gnuplot -c movieplot "${fingerprints}" $plot_dir
        gnuplot -c bitrate_ladder $bitrate_ladder_data $plot_dir

    done
}


compare_bitrates() {
    ids=($(ls -la | grep -v '[0-9]*_[0-9]*' | awk '$9 ~ /^[0-9]+/ {print $9}' | cut -d '_' -f1 | sort | uniq))

    for title in "${ids[@]}"; do
        title=$(echo $title | cut -d '.' -f1)
        files=$(ls -la | grep -v '[0-9]*_[0-9]*' | awk '$9 ~ /^[0-9]+/ {print $9}' | cut -d '_' -f1 | sort | uniq | grep $title)

        #TODO add metric for the error and plot it in the graph
        adu_file=$(echo $files | awk '{print $1}')
        har_file=$(echo $files | awk '{print $2}')

        filename=$(echo $files | cut -d '.' -f1).bl_comparison

        paste $adu_file $har_file > $filename 

        plot_dir="plots/"
        gnuplot -c bitrate_ladders $filename $plot_dir

    done
}

export -f get_adus
export -f get_hars
export -f build_fingerprint_db
export -f gen_plots
export -f compare_bitrates

#ls ../log/ | parallel get_adus 1
#get_hars 

#ls | grep -e '^[0-9]*_[0-9]*.adu$' | parallel build_fingerprint_db > db.adu
#ls | grep -e '^[0-9]*_[0-9]*.har$' | parallel build_fingerprint_db > db.har

#gen_plots "adu"
#gen_plots "har"

#compare_bitrates
#cd stats && cat * | awk '{if($15 < 0.17) print $15}' | wc -l 
bl_files=$(ls | grep -e '^[0-9]*_20000.adu.bl')
gnuplot -c scatter_ladder "${bl_files}"
cd stats && cat * | cut -d '&' -f1,2,3,4,7 | python ../k_means.py
#cat -n $bl_files | python k_means.py
#cd stats && tail * -n1 --quiet | awk '{sum+=$2; print $2} END {print sum / NR}' > avg_rmse
