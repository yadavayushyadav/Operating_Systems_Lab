# Usage (run from this folder):
#   make part1                                   baseline, creates output_part1.ppm
#   make part2_1_variantX                        threads + atomic operations          (X = 1 or 2)
#   make part2_2_variantX                        processes + pipes
#   make part2_3_variantX                        processes + shared memory + semaphores   (make part2_3 runs both variants)
#   make part3_1_variantX_A                      on computer A: S1 and S2             (start it first, it waits for computer B)
#   make part3_1_variantX_B HOST=<address of A>  on computer B: S3
#   make part3_2_variantX_A                      on computer A: S1
#   make part3_2_variantX_B HOST=<address of A>  on computer B: S2 and S3
#   make compare                                 compares every output_part*.ppm with the output of Lab 1
# Every target creates output_<target without _A/_B>.ppm, e.g. output_part2_1_variant1.ppm.
#
# Optional: INPUT=<ppm file>  RUNS=<number of runs>  BATCH=<pixels per message>  PORT=<port>  HOST=<address of computer A>
#           LAB1_OUTPUT=<the output of Lab 1 for the same input image>   (lab1_output.ppm is the Lab 1 output for input.ppm)

# -falign-functions=64 makes every function start at a 64-byte boundary. The three functions that process a
# pixel then have exactly the same position in every program. Without it the linker put S1_smoothen_pixel at a
# different address in each program, and on our processor the same instructions ran up to 45% slower in one
# program than in another, which would have spoiled the comparison of the programs (see the report).
FLAGS = -O2 -falign-functions=64

INPUT = input.ppm
RUNS = 1000
BATCH = 1
PORT = 12345
HOST = 127.0.0.1
LAB1_OUTPUT = lab1_output.ppm

# The targets below are commands, not files (part1, part2_1 ... are also the names of the source folders)
.PHONY: all clean compare part1 part2_3 \
	part2_1_variant1 part2_1_variant2 part2_2_variant1 part2_2_variant2 part2_3_variant1 part2_3_variant2 \
	part3_1_variant1_A part3_1_variant1_B part3_1_variant2_A part3_1_variant2_B \
	part3_2_variant1_A part3_2_variant1_B part3_2_variant2_A part3_2_variant2_B

all: part1.out part2_1.out part2_2.out part2_3.out part3_1_A.out part3_1_B.out part3_2_A.out part3_2_B.out

# Part I: baseline
part1.out: part1/part1.cpp libppm.cpp libppm.h
	g++ $(FLAGS) part1/part1.cpp libppm.cpp -o part1.out

part1: part1.out
	./part1.out $(INPUT) output_part1.ppm $(RUNS)

# Part II.1: three threads, atomic operations
part2_1.out: part2_1/part2_1.cpp libppm.cpp libppm.h
	g++ $(FLAGS) -pthread part2_1/part2_1.cpp libppm.cpp -o part2_1.out

part2_1_variant1: part2_1.out
	./part2_1.out $(INPUT) output_part2_1_variant1.ppm 1 $(RUNS) $(BATCH)

part2_1_variant2: part2_1.out
	./part2_1.out $(INPUT) output_part2_1_variant2.ppm 2 $(RUNS) $(BATCH)

# Part II.2: three processes, pipes
part2_2.out: part2_2/part2_2.cpp libppm.cpp libppm.h
	g++ $(FLAGS) part2_2/part2_2.cpp libppm.cpp -o part2_2.out

part2_2_variant1: part2_2.out
	./part2_2.out $(INPUT) output_part2_2_variant1.ppm 1 $(RUNS) $(BATCH)

part2_2_variant2: part2_2.out
	./part2_2.out $(INPUT) output_part2_2_variant2.ppm 2 $(RUNS) $(BATCH)

# Part II.3: three processes, shared memory and semaphores
part2_3.out: part2_3/part2_3.cpp libppm.cpp libppm.h
	g++ $(FLAGS) -pthread part2_3/part2_3.cpp libppm.cpp -o part2_3.out -lrt

part2_3_variant1: part2_3.out
	./part2_3.out $(INPUT) output_part2_3_variant1.ppm 1 $(RUNS) $(BATCH)

part2_3_variant2: part2_3.out
	./part2_3.out $(INPUT) output_part2_3_variant2.ppm 2 $(RUNS) $(BATCH)

part2_3: part2_3_variant1 part2_3_variant2

# Part III.1: computer A does S1 and S2, computer B does S3
part3_1_A.out: part3_1/part3_1_A.cpp libppm.cpp libppm.h
	g++ $(FLAGS) part3_1/part3_1_A.cpp libppm.cpp -o part3_1_A.out

part3_1_B.out: part3_1/part3_1_B.cpp libppm.cpp libppm.h
	g++ $(FLAGS) part3_1/part3_1_B.cpp libppm.cpp -o part3_1_B.out

part3_1_variant1_A: part3_1_A.out
	./part3_1_A.out $(INPUT) $(PORT) 1 $(RUNS) $(BATCH)

part3_1_variant1_B: part3_1_B.out
	./part3_1_B.out $(HOST) $(PORT) output_part3_1_variant1.ppm 1

part3_1_variant2_A: part3_1_A.out
	./part3_1_A.out $(INPUT) $(PORT) 2 $(RUNS) $(BATCH)

part3_1_variant2_B: part3_1_B.out
	./part3_1_B.out $(HOST) $(PORT) output_part3_1_variant2.ppm 2

# Part III.2: computer A does S1, computer B does S2 and S3
part3_2_A.out: part3_2/part3_2_A.cpp libppm.cpp libppm.h
	g++ $(FLAGS) part3_2/part3_2_A.cpp libppm.cpp -o part3_2_A.out

part3_2_B.out: part3_2/part3_2_B.cpp libppm.cpp libppm.h
	g++ $(FLAGS) part3_2/part3_2_B.cpp libppm.cpp -o part3_2_B.out

part3_2_variant1_A: part3_2_A.out
	./part3_2_A.out $(INPUT) $(PORT) 1 $(RUNS) $(BATCH)

part3_2_variant1_B: part3_2_B.out
	./part3_2_B.out $(HOST) $(PORT) output_part3_2_variant1.ppm 1

part3_2_variant2_A: part3_2_A.out
	./part3_2_A.out $(INPUT) $(PORT) 2 $(RUNS) $(BATCH)

part3_2_variant2_B: part3_2_B.out
	./part3_2_B.out $(HOST) $(PORT) output_part3_2_variant2.ppm 2

compare:
	./compare_outputs.sh $(LAB1_OUTPUT)

clean:
	rm -f *.out output_part*.ppm
