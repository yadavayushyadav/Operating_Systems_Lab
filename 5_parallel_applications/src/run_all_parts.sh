#!/bin/bash
# Runs every part of Assignment 5 on one image and compares all the outputs with the output of Lab 1.
# In Part III both "computers" are this computer: A and B are two processes that talk over 127.0.0.1.
#
# Usage: ./run_all_parts.sh [input.ppm] [lab1-output.ppm] [runs] [batch]
# Example: ./run_all_parts.sh ../../1_getting_started/lab1/images/3.ppm ../../1_getting_started/lab1/images/3o.ppm 100

input=${1:-input.ppm}
lab1_output=${2:-lab1_output.ppm}
runs=${3:-1000}
batch=${4:-1}
port=12345

make -s all || exit 1
rm -f output_part*.ppm

for target in part1 part2_1_variant1 part2_1_variant2 part2_2_variant1 part2_2_variant2 part2_3_variant1 part2_3_variant2; do
	echo "===== make $target"
	make -s $target INPUT=$input RUNS=$runs BATCH=$batch
done

for target in part3_1_variant1 part3_1_variant2 part3_2_variant1 part3_2_variant2; do
	echo "===== make ${target}_A   and   make ${target}_B"
	make -s ${target}_A INPUT=$input RUNS=$runs BATCH=$batch PORT=$port > computer_A.log 2>&1 &
	sleep 0.5		# computer A needs a moment to read the image and start waiting on the port
	make -s ${target}_B HOST=127.0.0.1 PORT=$port
	wait
	echo "----- computer A printed:"
	cat computer_A.log
	rm -f computer_A.log
done

echo "===== comparison with Lab 1"
./compare_outputs.sh $lab1_output
