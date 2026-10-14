# Usage: make sim_partX filename=<trace file in ../data>     (blender_memoryTrace.txt if filename is left out)
#        make sim_partX_all                                  (every trace in ../data)
# Part 4 also needs the physical memory size as a fraction of the virtual memory (a decimal or a ratio):
#        make sim_part4_1 filename=gcc_memoryTrace.txt fraction=1/1024
#        make sim_part4_3_all fraction=0.25

trace = $(if $(filename),../data/$(filename))
need_fraction = $(if $(fraction),,$(error Pass the physical memory size e.g. fraction=0.25 or fraction=1/1024))

all: part1.out part2.out part3_1.out part3_2.out part4_1.out part4_2.out part4_3.out part4_4.out

part1.out: part1.cpp
	g++ -O2 part1.cpp -o part1.out

sim_part1: part1.out
	./part1.out $(trace)

sim_part1_all: part1.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part1.out ../data/$$f; \
	done

part2.out: part2.cpp
	g++ -O2 part2.cpp -o part2.out

sim_part2: part2.out
	./part2.out $(trace)

sim_part2_all: part2.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part2.out ../data/$$f; \
	done

part3_1.out: part3_1.cpp
	g++ -O2 part3_1.cpp -o part3_1.out

sim_part3_1 sim_part3: part3_1.out
	./part3_1.out $(trace)

sim_part3_1_all: part3_1.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part3_1.out ../data/$$f; \
	done

part3_2.out: part3_2.cpp
	g++ -O2 part3_2.cpp -o part3_2.out

sim_part3_2: part3_2.out
	./part3_2.out $(trace)

sim_part3_2_all: part3_2.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part3_2.out ../data/$$f; \
	done

part4_1.out: part4_1.cpp
	g++ -O2 part4_1.cpp -o part4_1.out

sim_part4_1: part4_1.out
	$(need_fraction)
	./part4_1.out $(trace) $(fraction)

sim_part4_1_all: part4_1.out
	$(need_fraction)
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part4_1.out ../data/$$f $(fraction); \
	done

part4_2.out: part4_2.cpp
	g++ -O2 part4_2.cpp -o part4_2.out

sim_part4_2: part4_2.out
	$(need_fraction)
	./part4_2.out $(trace) $(fraction)

sim_part4_2_all: part4_2.out
	$(need_fraction)
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part4_2.out ../data/$$f $(fraction); \
	done

part4_3.out: part4_3.cpp
	g++ -O2 part4_3.cpp -o part4_3.out

sim_part4_3: part4_3.out
	$(need_fraction)
	./part4_3.out $(trace) $(fraction)

sim_part4_3_all: part4_3.out
	$(need_fraction)
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part4_3.out ../data/$$f $(fraction); \
	done

part4_4.out: part4_4.cpp
	g++ -O2 part4_4.cpp -o part4_4.out

sim_part4_4: part4_4.out
	$(need_fraction)
	./part4_4.out $(trace) $(fraction)

sim_part4_4_all: part4_4.out
	$(need_fraction)
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part4_4.out ../data/$$f $(fraction); \
	done

clean:
	rm -f *.out
