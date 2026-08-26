#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;

int main(int argc, char **argv)
{
	if(argc != 6)
	{
		cout <<"usage: ./partitioner.out <path-to-file> <pattern> <search-start-position> <search-end-position> <max-chunk-size>\nprovided arguments:\n";
		for(int i = 0; i < argc; i++)
			cout << argv[i] << "\n";
		return -1;
	}
	
	char *file_to_search_in = argv[1];
	char *pattern_to_search_for = argv[2];
	int search_start_position = atoi(argv[3]);
	int search_end_position = atoi(argv[4]);
	int max_chunk_size = atoi(argv[5]);
	
	pid_t my_pid = getpid();
	int search_size = search_end_position - search_start_position + 1;

	cout << "[" << my_pid << "] start position = " << search_start_position << " ; end position = " << search_end_position << "\n";
	cout.flush();

	if(search_size > max_chunk_size)
	{
		int left_end_position = search_start_position + search_size / 2 - 1;
		string left_start = to_string(search_start_position);
		string left_end = to_string(left_end_position);
		string right_start = to_string(left_end_position + 1);
		string right_end = to_string(search_end_position);
		pid_t my_children[2];

		my_children[0] = fork();
		if(my_children[0] == 0)
		{
			execl(argv[0], argv[0], file_to_search_in, pattern_to_search_for, left_start.c_str(), left_end.c_str(), argv[5], (char *)NULL);
			return -1;
		}
		cout << "[" << my_pid << "] forked left child " << my_children[0] << "\n";
		cout.flush();

		my_children[1] = fork();
		if(my_children[1] == 0)
		{
			execl(argv[0], argv[0], file_to_search_in, pattern_to_search_for, right_start.c_str(), right_end.c_str(), argv[5], (char *)NULL);
			return -1;
		}
		cout << "[" << my_pid << "] forked right child " << my_children[1] << "\n";
		cout.flush();

		for(int i = 0; i < 2; i++)
		{
			pid_t finished_child = wait(NULL);
			if(finished_child == my_children[0])
				cout << "[" << my_pid << "] left child returned\n";
			else
				cout << "[" << my_pid << "] right child returned\n";
			cout.flush();
		}
	}
	else
	{
		pid_t searcher_pid = fork();
		if(searcher_pid == 0)
		{
			execl("./part2_searcher.out", "./part2_searcher.out", file_to_search_in, pattern_to_search_for, argv[3], argv[4], (char *)NULL);
			return -1;
		}
		cout << "[" << my_pid << "] forked searcher child " << searcher_pid << "\n";
		cout.flush();

		waitpid(searcher_pid, NULL, 0);
		cout << "[" << my_pid << "] searcher child returned\n";
		cout.flush();
	}

	return 0;
}
