#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <signal.h>

using namespace std;

int main(int argc, char **argv)
{
	if(argc != 5)
	{
		cout <<"usage: ./partitioner.out <path-to-file> <pattern> <search-start-position> <search-end-position>\nprovided arguments:\n";
		for(int i = 0; i < argc; i++)
			cout << argv[i] << "\n";
		return -1;
	}
	
	char *file_to_search_in = argv[1];
	char *pattern_to_search_for = argv[2];
	int search_start_position = atoi(argv[3]);
	int search_end_position = atoi(argv[4]);

	ifstream input_file(file_to_search_in, ios::binary);
	if(!input_file)
	{
		cout << "[" << getpid() << "] could not open " << file_to_search_in << "\n";
		return -1;
	}

	int chunk_size = search_end_position - search_start_position + 1;
	char *chunk = new char[chunk_size + 1];

	input_file.seekg(search_start_position);
	input_file.read(chunk, chunk_size);
	chunk[input_file.gcount()] = '\0';
	input_file.close();

	char *match = strstr(chunk, pattern_to_search_for);
	int found_position = -1;
	if(match != NULL)
		found_position = search_start_position + (match - chunk);
	delete[] chunk;

	if(found_position != -1)
	{
		cout << "[" << getpid() << "] found at " << found_position << "\n";
		return 1;
	}
	
	cout << "[" << getpid() << "] didn't find\n";
	return 0;
}
