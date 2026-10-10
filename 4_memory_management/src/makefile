part1.out: part1.cpp
	g++ -O2 part1.cpp -o part1.out

sim_part1: part1.out
	./part1.out ../data/$(filename)

sim_part1_all: part1.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part1.out ../data/$$f; \
	done

part2.out: part2.cpp
	g++ -O2 part2.cpp -o part2.out

sim_part2: part2.out
	./part2.out ../data/$(filename)

sim_part2_all: part2.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part2.out ../data/$$f; \
	done

part3_1.out: part3_1.cpp
	g++ -O2 part3_1.cpp -o part3_1.out

sim_part3: part3_1.out
	./part3_1.out ../data/$(filename)

sim_part3_1_all: part3_1.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part3_1.out ../data/$$f; \
	done

part3_2.out: part3_2.cpp
	g++ -O2 part3_2.cpp -o part3_2.out

sim_part3_2: part3_2.out
	./part3_2.out ../data/$(filename)

sim_part3_2_all: part3_2.out
	@for f in $$(ls ../data); do \
		echo "== $$f =="; \
		./part3_2.out ../data/$$f; \
	done

clean: 
	rm -f *.out