#!/bin/bash


#TODO

#- Add information on processing
#- Adjust legends in the plots

mkdir plots

global=""
c=0

read_elapsed_time () {
    awk '/^ADU/ {if(c++==0) {d1=$1}} END{printf("%.2f%s\t", $1-d1, " secs")}' $1
}

for file in ../log/*; do
    filename="${file##*/}"

    #awk '/^ADU/ {if(c++>0 && $4=="<1" && $6>20000){time_diff=$2-_n; cum_time+=time_diff; printf("%.7f\t%s\n", cum_time, $6)};{_n=$2}}' $file >> ./$filename.dat

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

    filename="${record##*/}"
    echo $filename

    id=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $1}')
    tp=$(echo $filename | awk -F\| '{sub(/_/,"|");$1=$1;printf "%d\n", $2}')

    echo $id $tp "\n" $record >> db.dat
    #cat $record >> db.dat
    #echo '\n ------------------------ \n' >> db.dat
done

#gnuplot -e "files='${global}'" globalplot
