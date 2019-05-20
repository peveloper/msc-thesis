#!/bin/bash

# filter logs and save each fingerprint in text file, finally git push

for file in /home/ubuntu/msc-thesis/acquire_fingerprints/log/*; do
    grep -e ADU $file | awk '$6 > 200000' | cut -d" " -f2,3,4,5,6 | python preprocessor.py | awk '{SUM += $3} END { print $1, $2, $3, SUM }'
done


