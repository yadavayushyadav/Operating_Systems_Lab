# Lab 4: Memory Management

Ayush Yadav (CS24BT001), Trisham Bepari (CS24BT022)

A memory virtualization simulator written in C++. It models a single-level page table, a two-level page table and a 128-entry LRU TLB, followed by a physical memory smaller than the virtual memory with LRU page replacement. It runs on the ten benchmark traces given with the assignment.

## Folder structure

```
4_memory_management/
├── 4_memory_management.pdf   assignment handout
├── README.md                 this file
├── src/                      source code and makefile
│   ├── makefile
│   ├── part1.cpp             Part I:   single-level page table
│   ├── part2.cpp             Part II:  two-level page table
│   ├── part3_1.cpp           Part III: single-level page table + TLB
│   ├── part3_2.cpp           Part III: two-level page table + TLB
│   ├── part4_1.cpp           Part IV:  single-level page table, limited physical memory (LRU replacement)
│   ├── part4_2.cpp           Part IV:  two-level page table, limited physical memory
│   ├── part4_3.cpp           Part IV:  single-level page table + TLB, limited physical memory
│   └── part4_4.cpp           Part IV:  two-level page table + TLB, limited physical memory
├── data/                     input traces: <benchmark>_memoryTrace.txt (10 files, ~1.5 GB)
├── results/                  access count of every page, written by Parts I-III
│   ├── part1/                <benchmark>_memoryTrace_pagecounts.csv  (columns: vpn,count)
│   ├── part2/
│   └── part3/
│       ├── single_level_tlb/
│       └── two_level_tlb/
└── report/
    ├── report.tex            report source (graphs are drawn with pgfplots)
    ├── report.pdf            compiled report
    └── data/                 numbers plotted in the report (.dat files)
```

`data/` and `results/` are not in the submission zip: `data/` holds the large input traces, and `results/` is regenerated automatically when Parts I-III are run. There is no separate program for Part V; every simulator prints its own simulation time.

## Building and running

Run everything from `src/`. The traces are expected in `../data/`.

```
make all                                                  # build every part
make sim_part1 filename=gcc_memoryTrace.txt              # one trace (blender if filename is left out)
make sim_part1_all                                        # every trace in ../data
make sim_part4_3 filename=gcc_memoryTrace.txt fraction=1/1024
make sim_part4_1_all fraction=0.25
make clean
```

The same targets exist for `part2`, `part3_1`, `part3_2` and `part4_1` to `part4_4`. Part IV also needs `fraction`, the physical memory size as a fraction of the 4 GB virtual memory, written either as a decimal (`0.25`) or as a ratio (`1/1024`). With these traces, page replacements start only below `1/64`.

## Output

Every program prints the number of accesses, frames used, total cycles, the hash of all the frame numbers and the simulation time. Depending on the part, it also prints:

- **Parts I-II:** page table size and page table utilization.
- **Part III:** TLB hits, misses and hit rate.
- **Part IV:** page faults, page fault rate and page replacements (disk accesses), plus the TLB statistics for `part4_3` and `part4_4`.

## Submission zip

`CS24BT001_CS24BT022_Assignment_4.zip` contains:

```
README.md
CS24BT001_CS24BT022_Assignment4_report.pdf
src/makefile
src/part1.cpp  src/part2.cpp  src/part3_1.cpp  src/part3_2.cpp
src/part4_1.cpp  src/part4_2.cpp  src/part4_3.cpp  src/part4_4.cpp
```

To run it, extract the zip and put the trace files in a `data/` folder next to `src/`.
