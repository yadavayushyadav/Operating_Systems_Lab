# Lab 5: Parallel Applications

Ayush Yadav (CS24BT001), Trisham Bepari (CS24BT022)

The image sharpener of Lab 1 (S1 smoothen, S2 find details, S3 sharpen), written as a pipeline in which each stage runs on its own core and passes its pixels to the next stage as they get ready. The pipeline is built in four ways: three threads with atomic operations, three processes with pipes, three processes with shared memory and semaphores, and two computers with sockets. Every version produces exactly the image that Lab 1 produced.

## Folder structure

```
5_parallel_applications/
├── 5_parallel_applications.pdf   assignment handout
├── README.md                     this file
├── src/
│   ├── makefile
│   ├── input.ppm                 input image (image 1 of Lab 1, 450 x 180)
│   ├── lab1_output.ppm           what the Lab 1 program wrote for input.ppm (copy of lab1/images/1o.ppm)
│   ├── output_part*.ppm          the 11 images written by the make targets below (all identical to lab1_output.ppm)
│   ├── libppm.cpp, libppm.h      PPM reader/writer of Lab 1
│   ├── part1/part1.cpp           Part I:     baseline, no parallelism
│   ├── part2_1/part2_1.cpp       Part II.1:  three threads, atomic operations (test_and_set spin lock)
│   ├── part2_2/part2_2.cpp       Part II.2:  three processes, pipes
│   ├── part2_3/part2_3.cpp       Part II.3:  three processes, shared memory + semaphores
│   ├── part3_1/part3_1_A.cpp     Part III.1: computer A (S1 and S2)
│   ├── part3_1/part3_1_B.cpp     Part III.1: computer B (S3)
│   ├── part3_2/part3_2_A.cpp     Part III.2: computer A (S1)
│   ├── part3_2/part3_2_B.cpp     Part III.2: computer B (S2 and S3)
│   ├── compare_outputs.sh        compares every output with the output of Lab 1
│   ├── run_all_parts.sh          runs every part and then compare_outputs.sh
│   └── run_experiments.sh        timing experiments of the report
├── measurements/                 raw measurements: timings.csv (576 timed runs), usage.csv, strace_*.txt,
│                                 verification_summary.txt (comparison with Lab 1 on all 7 images)
├── report/
│   ├── report.tex                report source (graphs are drawn with pgfplots)
│   ├── report.pdf                compiled report
│   └── plots/                    numbers plotted in the report (.dat files) and make_data.py, which
│                                 computes them from measurements/timings.csv
└── CS24BT001_CS24BT022_Assignment_5.zip   the submission
```

## Building and running

Run everything from `src/`.

```
make part1                      # baseline                        -> output_part1.ppm
make part2_1_variant1           # threads + atomic operations     -> output_part2_1_variant1.ppm
make part2_1_variant2
make part2_2_variant1           # processes + pipes               -> output_part2_2_variant1.ppm
make part2_2_variant2
make part2_3_variant1           # processes + shared memory + semaphores
make part2_3_variant2
make part2_3                    # both variants of part2_3
make compare                    # compares every output_part*.ppm with the Lab 1 output
make clean
```

Options (all have defaults):

| Option | Default | Meaning |
|---|---|---|
| `INPUT` | `input.ppm` | input image |
| `RUNS` | `1000` | how many times S1, S2 and S3 are run (a multiple of 100 in variant 2) |
| `BATCH` | `1` | pixels per message. 1 = every pixel is passed as soon as it is ready |
| `PORT` | `12345` | Part III: port on which computer A waits |
| `HOST` | `127.0.0.1` | Part III: address of computer A (used on computer B) |
| `LAB1_OUTPUT` | `lab1_output.ppm` | `make compare`: the Lab 1 output for the same input |

With the default `BATCH=1` the variant 1 targets of Parts II and III take about a minute each, because 81 million pixels are passed one at a time. `make part2_2_variant1 BATCH=450` passes one row of the image per message and finishes in about two seconds.

**Variant 1:** every run of a stage passes its result to the next stage. **Variant 2:** only every 100th run of S1 passes its result; S2 runs 100 times on each result it receives and passes the result of the 100th run, and so does S3.

## Part III on two computers

Computer A is the server and computer B the client. Copy `src/` to both computers.

```
# on computer A (find its address with: ip a)
make part3_1_variant1_A                      # reads input.ppm, then waits for computer B on port 12345

# on computer B
make part3_1_variant1_B HOST=<address of A>  # connects, runs S3, writes output_part3_1_variant1.ppm
```

The same holds for `part3_1_variant2`, `part3_2_variant1` and `part3_2_variant2`. Use the same variant on both computers (computer B refuses to run otherwise). Computer B does not need the input image: computer A sends it once at the start.

If computer B cannot connect, a firewall is probably blocking the port. On computer A: `sudo ufw allow 12345/tcp` (or `sudo ufw disable`).

Both programs can also be run on one computer, in two terminals, with the default `HOST=127.0.0.1`. `./run_all_parts.sh` does this. The Part III numbers in the report were measured this way (two processes on one computer), because a second computer was not available.

## Output

Every program prints the image size, the read, processing and write times, and (for the parallel versions) the time after which each stage finished. The parallel versions also print, for every link between two stages, the number of pixels sent and received and an order-dependent hash of them; `Check: PASSED` means the pixels were received as sent, in the sent order.

## Comparing with Lab 1

```
./run_all_parts.sh                           # runs all 11 targets on input.ppm, then compares
./compare_outputs.sh                         # only compares the output_part*.ppm that are there
./run_all_parts.sh ../../1_getting_started/lab1/images/3.ppm ../../1_getting_started/lab1/images/3o.ppm 100 450
                                             # another image: <input> <lab1 output> <runs> <batch>
```

`compare_outputs.sh` compares each output byte by byte with the image that the Lab 1 program wrote for the same input. For `input.ppm` that image is `lab1_output.ppm`; for the other Lab 1 images it is `<n>o.ppm` in the Lab 1 `images` folder.

## Compiler flags

Everything is built with `g++ -O2 -falign-functions=64`. The second flag starts every function at a 64-byte boundary, so that the functions that process a pixel have the same position in every program. Without it the same function ran up to 45% slower in one program than in another, only because of the address the linker gave it, and the programs could not be compared fairly (see the report).

## Submission zip

`CS24BT001_CS24BT022_Assignment_5.zip` contains:

```
README.md
CS24BT001_CS24BT022_Assignment5_report.pdf
src/makefile
src/input.ppm  src/lab1_output.ppm
src/libppm.cpp  src/libppm.h
src/part1/  src/part2_1/  src/part2_2/  src/part2_3/  src/part3_1/  src/part3_2/     one folder per question
src/compare_outputs.sh  src/run_all_parts.sh  src/run_experiments.sh
```

Everything in the zip works on its own, except `run_experiments.sh`, which also needs the seven images of Lab 1 (`../../1_getting_started/lab1/images`).
