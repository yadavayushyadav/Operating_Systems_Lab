CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
TARGET    = scheduler
SRC       = scheduler.cpp

WL_DIR    = workloads
RES_DIR   = results

.PHONY: all clean gen-workloads experiments report distclean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

# Synthetic workloads, for reproducing the report without the course files.
# NB: the target is deliberately not named "workloads", which is the name of
# the output directory -- make would consider it up to date once it exists.
gen-workloads:
	mkdir -p $(WL_DIR)
	python3 gen_workload.py --n 40 --mix cpu    --seed 3 > $(WL_DIR)/cpu_bound.txt
	python3 gen_workload.py --n 40 --mix io     --seed 5 > $(WL_DIR)/io_bound.txt
	python3 gen_workload.py --n 40 --mix mixed  --seed 7 > $(WL_DIR)/mixed.txt
	python3 gen_workload.py --n 40 --mix starve --seed 13 --arrival-gap 3 > $(WL_DIR)/starve.txt

experiments: $(TARGET)
	python3 experiments.py --workload-dir $(WL_DIR) --out $(RES_DIR)

report: experiments
	pdflatex -interaction=nonstopmode report.tex >/dev/null
	pdflatex -interaction=nonstopmode report.tex >/dev/null

clean:
	rm -f $(TARGET) *.o *.aux *.log *.out *.toc

distclean: clean
	rm -rf $(RES_DIR) report.pdf
