#!/bin/bash
# Timing experiments of the report. Every run appends one line to ../measurements/timings.csv:
#   experiment,program,variant,batch,image,runs,repetition,read,processing,write,total,s1,s2,s3,correct
# s1, s2, s3 : for part1 the time spent in S1, S2 and S3; for the parallel programs the time after which
#              each stage finished (for Part III s1 is the time after which computer A finished).
# correct    : 1 if the output is identical to the Lab 1 output and every link check passed.
# In Part III computer A and computer B are two processes on this computer that talk over 127.0.0.1.
#
# Usage: ./run_experiments.sh [main] [batch] [images] [single] [small] [usage]      (all six if nothing is given)

images=../../1_getting_started/lab1/images
results=../measurements
csv=$results/timings.csv
port=12345
log=$results/last_run.log
output=output_experiment.ppm

mkdir -p $results
make -s all || exit 1
[ -f $csv ] || echo "experiment,program,variant,batch,image,runs,repetition,read,processing,write,total,s1,s2,s3,correct" > $csv

# value "<label>" <file> : the number of the line "<label> ... : <number> s"
value() {
	grep -m1 "$1" $2 | awk -F'\t' '{print $NF}' | awk '{print $1}'
}

image_width() {
	grep -a -v '^#' $images/$1.ppm | head -2 | tail -1 | awk '{print $1}'
}

# run_one <experiment> <program> <variant> <batch> <image> <runs> <repetition>
run_one() {
	experiment=$1; program=$2; variant=$3; batch=$4; image=$5; runs=$6; repetition=$7
	rm -f $output $log $log.B
	checks=2
	case $program in
		part1)
			./part1.out $images/$image.ppm $output $runs > $log 2>&1
			checks=0 ;;
		part2_*)
			./$program.out $images/$image.ppm $output $variant $runs $batch > $log 2>&1 ;;
		part3_*)
			./${program}_A.out $images/$image.ppm $port $variant $runs $batch > $log 2>&1 &
			until grep -q "Waiting for computer B" $log 2>/dev/null; do sleep 0.05; done
			./${program}_B.out 127.0.0.1 $port $output $variant > $log.B 2>&1
			wait
			cat $log.B >> $log ;;
	esac

	correct=0
	if cmp -s $output $images/${image}o.ppm && ! grep -q FAILED $log && [ $(grep -c PASSED $log) -eq $checks ]; then correct=1; fi

	if [ $program = part1 ]; then
		s1=$(value "S1 Time" $log); s2=$(value "S2 Time" $log); s3=$(value "S3 Time" $log)
	elif [ ${program:0:5} = part3 ]; then
		s1=$(value "A Finished After" $log); s2=; s3=
	else
		s1=$(value "S1 Finished After" $log); s2=$(value "S2 Finished After" $log); s3=$(value "S3 Finished After" $log)
	fi
	line="$experiment,$program,$variant,$batch,$image,$runs,$repetition,$(value "File Read Time" $log),$(value "Processing Time" $log),$(value "File Write Time" $log),$(value "Total Time" $log),$s1,$s2,$s3,$correct"
	echo $line >> $csv
	echo $line
	rm -f $output
	sleep 2			# let the processor cool down a little, so that every run starts in a similar state
}

# 1. Every program, both variants, pixel by pixel (batch = 1), image 1, 1000 runs
experiment_main() {
	for repetition in 1 2 3 4 5; do
		run_one main part1 0 0 1 1000 $repetition
		for program in part2_1 part2_2 part2_3 part3_1 part3_2; do
			run_one main $program 2 1 1 1000 $repetition
			if [ $repetition -le 3 ]; then run_one main $program 1 1 1 1000 $repetition; fi
		done
	done
}

# 2. Effect of the number of pixels per message, image 1, 1000 runs
experiment_batch() {
	for repetition in 1 2 3; do
		run_one batch part1 0 0 1 1000 $repetition
		for batch in 2 4 8 16 32 64 128 256 450 1024 4096 16384 81000; do
			for program in part2_1 part2_2 part2_3; do
				run_one batch $program 1 $batch 1 1000 $repetition
			done
		done
		for batch in 16 450 81000; do
			for program in part3_1 part3_2; do
				run_one batch $program 1 $batch 1 1000 $repetition
			done
		done
		for program in part2_1 part2_2 part2_3 part3_1 part3_2; do
			run_one batch $program 2 450 1 1000 $repetition
		done
	done
}

# 3. Images of different sizes, 100 runs, one row of the image per message
experiment_images() {
	for repetition in 1 2 3; do
		for image in 1 2 3 4 5 6 7; do
			width=$(image_width $image)
			run_one images part1 0 0 $image 100 $repetition
			for program in part2_1 part2_2 part2_3; do
				run_one images $program 1 $width $image 100 $repetition
				run_one images $program 2 $width $image 100 $repetition
			done
		done
	done
}

# 4. The original experiment: a single run, where reading and writing the files takes most of the time
experiment_single() {
	for repetition in 1 2 3 4 5; do
		for image in 1 4 7; do
			width=$(image_width $image)
			run_one single part1 0 0 $image 1 $repetition
			for program in part2_1 part2_2 part2_3; do
				run_one single $program 1 1 $image 1 $repetition
				run_one single $program 1 $width $image 1 $repetition
			done
		done
	done
}

# 5. Image 1 again, for the experiments 3 and 4. Its runs take only 0.02 to 0.5 s, so they are disturbed
#    easily (for example when they come right after the long runs of image 7, while the processor is still
#    hot). They are repeated here on their own, more often.
experiment_small() {
	width=$(image_width 1)
	for repetition in 1 2 3 4 5 6 7 8 9; do
		run_one small part1 0 0 1 100 $repetition
		run_one small part1 0 0 1 1 $repetition
		for program in part2_1 part2_2 part2_3; do
			run_one small $program 1 $width 1 100 $repetition
			run_one small $program 2 $width 1 100 $repetition
			run_one small $program 1 1 1 1 $repetition
			run_one small $program 1 $width 1 1 $repetition
		done
	done
}

# 6. Processor time and context switches (/usr/bin/time), and system calls (strace), image 1, variant 1
experiment_usage() {
	usage=$results/usage.csv
	echo "program,batch,runs,elapsed,user,system,voluntary_switches,involuntary_switches" > $usage
	for batch in 1 450; do
		for program in part1 part2_1 part2_2 part2_3; do
			arguments="1 100 $batch"
			if [ $program = part1 ]; then arguments="100"; fi
			/usr/bin/time -f "%e,%U,%S,%w,%c" -o $log.time ./$program.out $images/1.ppm $output $arguments > $log 2>&1
			echo "$program,$batch,100,$(cat $log.time)" | tee -a $usage
			sleep 2
		done
	done
	for program in part2_1 part2_2 part2_3; do
		strace -f -c -o $results/strace_${program}_batch1.txt ./$program.out $images/1.ppm $output 1 2 1 > /dev/null 2>&1
		strace -f -c -o $results/strace_${program}_batch450.txt ./$program.out $images/1.ppm $output 1 2 450 > /dev/null 2>&1
	done
	rm -f $output $log.time
}

if [ $# -eq 0 ]; then set -- main batch images single small usage; fi
for experiment in "$@"; do
	echo "########## experiment: $experiment ($(date +%H:%M:%S))"
	experiment_$experiment
done
rm -f $log $log.B
echo "########## done ($(date +%H:%M:%S))"
