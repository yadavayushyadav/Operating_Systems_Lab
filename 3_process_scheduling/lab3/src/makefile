FILE_NUMBER = 1
TIME_QUANTUM = 2
BOOST_INTERVAL = 20
TRACE =

single_processor.out: single_processor.cpp
	g++ -g single_processor.cpp -o single_processor.out

dual_processor.out: dual_processor.cpp
	g++ -g dual_processor.cpp -o dual_processor.out

run-fifo-single: single_processor.out
	./single_processor.out FIFO ../processes/process$(FILE_NUMBER).txt $(TRACE)

run-round-robin-single: single_processor.out
	./single_processor.out RR ../processes/process$(FILE_NUMBER).txt $(TIME_QUANTUM) $(TRACE)

run-mlfq-no-boost-single: single_processor.out
	./single_processor.out MLFQ ../processes/process$(FILE_NUMBER).txt 0 $(TRACE)

run-mlfq-with-boost-single: single_processor.out
	./single_processor.out MLFQ ../processes/process$(FILE_NUMBER).txt $(BOOST_INTERVAL) $(TRACE)

run-fifo-dual: dual_processor.out
	./dual_processor.out FIFO ../processes/process$(FILE_NUMBER).txt $(TRACE)

run-round-robin-dual: dual_processor.out
	./dual_processor.out RR ../processes/process$(FILE_NUMBER).txt $(TIME_QUANTUM) $(TRACE)

run-mlfq-no-boost-dual: dual_processor.out
	./dual_processor.out MLFQ ../processes/process$(FILE_NUMBER).txt 0 $(TRACE)

run-mlfq-with-boost-dual: dual_processor.out
	./dual_processor.out MLFQ ../processes/process$(FILE_NUMBER).txt $(BOOST_INTERVAL) $(TRACE)

clean:
	rm -f *.out
