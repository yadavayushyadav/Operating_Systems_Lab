#!/bin/bash
# Compares the outputs of Assignment 5 with the original output of Assignment 1.
#
# Usage: ./compare_outputs.sh [lab1-output.ppm]        (or: make compare LAB1_OUTPUT=<file>)
#
# The argument is the image that the Lab 1 program produced for the same input image. By default it is
# lab1_output.ppm, a copy of 1_getting_started/lab1/images/1o.ppm: the original Lab 1 output for image 1
# (input.ppm here is that image). Every part of Assignment 5 must produce exactly the same file, so each
# output_part*.ppm of this folder is compared with it byte by byte.

lab1_output=${1:-lab1_output.ppm}

expected_outputs="output_part1.ppm
output_part2_1_variant1.ppm output_part2_1_variant2.ppm
output_part2_2_variant1.ppm output_part2_2_variant2.ppm
output_part2_3_variant1.ppm output_part2_3_variant2.ppm
output_part3_1_variant1.ppm output_part3_1_variant2.ppm
output_part3_2_variant1.ppm output_part3_2_variant2.ppm"

if [ ! -f "$lab1_output" ]; then
	echo "Lab 1 output $lab1_output not found"
	exit 1
fi

echo "Lab 1 output : $lab1_output ($(stat -c %s "$lab1_output") bytes, md5 $(md5sum < "$lab1_output" | cut -d' ' -f1))"
echo

identical=0
different=0
missing=0
for output in $expected_outputs; do
	if [ ! -f "$output" ]; then
		printf "%-30s : not created yet\n" "$output"
		missing=$((missing + 1))
	elif cmp -s "$lab1_output" "$output"; then
		printf "%-30s : IDENTICAL to the Lab 1 output\n" "$output"
		identical=$((identical + 1))
	else
		# cmp -l prints one line for every byte that differs
		bytes=$(cmp -l "$lab1_output" "$output" 2>/dev/null | wc -l)
		printf "%-30s : DIFFERENT (%s bytes, %s differing bytes)\n" "$output" "$(stat -c %s "$output")" "$bytes"
		different=$((different + 1))
	fi
done

echo
echo "$identical identical, $different different, $missing not created yet"
[ $different -eq 0 ] && [ $missing -eq 0 ]
