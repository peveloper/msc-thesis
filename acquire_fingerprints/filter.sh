#!/bin/bash

for file in /home/ubuntu/msc-thesis/acquire_fingerprints/log/*; do
  "$file" | grep ADU | awk '$6 > 200000' | cut -d" " -f2,3,4,5,6 | python preprocessor.py
done
