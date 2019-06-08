#!/bin/bash


#TODO

#- Add information on processing
#- Adjust legends in the plots

mkdir plots

plot_string=""
c=0

for file in ../log/*; do
    filename="${file##*/}"

    awk '/^ADU/ {if(c++>0 && $4=="<1" && $6>1000){time_diff=$2-_n; cum_time+=time_diff; printf("%.7f\t%s\n", cum_time, $6)};{_n=$2}}' $file >> ./$filename.dat

    gnuplot -e "file='${filename}.dat'" singleplot

    ((c++))

    if (( $c > 1 ))
    then
        plot_string="${plot_string} ${filename}"
    else
        plot_string="${filename}"
    fi
done

for record in *.dat; do

    if [ $record = "db.dat" ]
    then
        break
    fi
    cat $record >> db.dat
    echo '\n ------------------------ \n' >> db.dat
done

gnuplot -e "files='${plot_string}'" globalplot
