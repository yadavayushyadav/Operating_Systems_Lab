// lab 5 > src > part2_2 > part2_2.cpp

// Part II.2: S1, S2 and S3 are three different processes that communicate via pipes.
// The parent reads the image once, creates two pipes (S1 -> S2 and S2 -> S3) and forks one child per stage.
// The children get their own copy of the input image from fork(); only the results travel through the pipes.
// A pipe is a byte stream kept by the kernel: write() blocks while the pipe is full and read() blocks while
// it is empty, so the pipe does the synchronization by itself.
//
// Pixels are passed in row-major order, in messages of <batch> pixels (one write() per message). By default
// batch = 1, i.e. every pixel is passed as soon as it is ready.
// Variant 1: every run of a stage passes its result to the next stage.
// Variant 2: only every 100th run of S1 passes its result. S2 runs 100 times on every result it receives
//            and passes the result of the 100th run, and S3 does the same.

#include <iostream>
#include "../libppm.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <sys/wait.h>

using namespace std;

const uint64_t HASH_START = 1469598103934665603ULL;
const uint64_t HASH_PRIME = 1099511628211ULL;

struct image_t *input_image;
int runs, variant, batch;
int runs_per_result;					// runs of a stage for every result it passes on: 1 in variant 1, 100 in variant 2

int pipe_1[2];							// S1 -> S2: [0] is the read end, [1] is the write end
int pipe_2[2];							// S2 -> S3

chrono::steady_clock::time_point t_processing;

struct image_t* allocate_image(int width, int height)
{
	image_t *image = new struct image_t;
	image->width = width;
	image->height = height;
	image->image_pixels = new uint8_t**[height];
	for (int i = 0; i < height; i++)
	{
		image->image_pixels[i] = new uint8_t*[width];
		for (int j = 0; j < width; j++) image->image_pixels[i][j] = new uint8_t[3];
	}
	return image;
}

// S1 for one pixel: the average of the 3x3 neighbourhood of pixel (i, j). Pixels on the border average
// over the neighbours they actually have.
void S1_smoothen_pixel(struct image_t *input_image, int i, int j, uint8_t *smoothened_pixel)
{
	uint16_t sum[3] = {0, 0, 0};
	uint8_t count = 0;
	for (int x = -1; x <= 1; x++)
	{
		for (int y = -1; y <= 1; y++)
		{
			if (i + x >= 0 && i + x < input_image->height && j + y >= 0 && j + y < input_image->width)
			{
				sum[0] += input_image->image_pixels[i + x][j + y][0];
				sum[1] += input_image->image_pixels[i + x][j + y][1];
				sum[2] += input_image->image_pixels[i + x][j + y][2];
				count++;
			}
		}
	}
	smoothened_pixel[0] = sum[0] / count;
	smoothened_pixel[1] = sum[1] / count;
	smoothened_pixel[2] = sum[2] / count;
}

// S2 for one pixel: details = input - smoothened, with negative differences clamped to 0
void S2_find_details_pixel(uint8_t *input_pixel, uint8_t *smoothened_pixel, uint8_t *details_pixel)
{
	details_pixel[0] = (input_pixel[0] > smoothened_pixel[0])? (input_pixel[0] - smoothened_pixel[0]) : 0;
	details_pixel[1] = (input_pixel[1] > smoothened_pixel[1])? (input_pixel[1] - smoothened_pixel[1]) : 0;
	details_pixel[2] = (input_pixel[2] > smoothened_pixel[2])? (input_pixel[2] - smoothened_pixel[2]) : 0;
}

// S3 for one pixel: sharpened = input + details, clamped at 255
void S3_sharpen_pixel(uint8_t *input_pixel, uint8_t *details_pixel, uint8_t *sharpened_pixel)
{
	sharpened_pixel[0] = min(255, input_pixel[0] + details_pixel[0]);
	sharpened_pixel[1] = min(255, input_pixel[1] + details_pixel[1]);
	sharpened_pixel[2] = min(255, input_pixel[2] + details_pixel[2]);
}

// Adds pixels to a hash that depends on their order (the hash of Lab 4). The sender hashes the pixels in
// the order it sends them and the receiver in the order it receives them, so the two hashes are equal only
// if the same pixels arrived in the same order.
uint64_t hash_pixels(uint64_t hash, uint8_t *pixels, int number_of_pixels)
{
	for (int i = 0; i < number_of_pixels; i++)
	{
		hash ^= (pixels[3 * i] << 16) | (pixels[3 * i + 1] << 8) | pixels[3 * i + 2];
		hash *= HASH_PRIME;
	}
	return hash;
}

// write() may write fewer bytes than asked (when the pipe is almost full), so it is repeated until all are written
void send_bytes(int fd, void *bytes, size_t size)
{
	size_t done = 0;
	while (done < size)
	{
		ssize_t written = write(fd, (uint8_t *) bytes + done, size - done);
		if (written <= 0)
		{
			perror("write");
			exit(1);
		}
		done += written;
	}
}

// read() returns as soon as some bytes are in the pipe, so it is repeated until the whole message has arrived.
// It returns 0 only when the pipe is empty and every write end is closed, which must not happen in the middle.
void receive_bytes(int fd, void *bytes, size_t size)
{
	size_t done = 0;
	while (done < size)
	{
		ssize_t received = read(fd, (uint8_t *) bytes + done, size - done);
		if (received <= 0)
		{
			if (received == 0) cerr << "[" << getpid() << "] the pipe was closed before all the pixels arrived" << endl;
			else perror("read");
			exit(1);
		}
		done += received;
	}
}

double seconds_since(chrono::steady_clock::time_point from)
{
	return chrono::duration<double>(chrono::steady_clock::now() - from).count();
}

// The receiver compares what it received with what the sender says it sent (the two numbers that the sender
// writes into the pipe after its last pixel).
bool check_link(const char *link, uint64_t received, uint64_t received_hash, uint64_t sent, uint64_t sent_hash)
{
	bool in_order = (sent == received && sent_hash == received_hash);
	cout << link << " Sent \t\t : \t" << sent << " pixels, hash " << sent_hash << "\n"
	     << link << " Received \t : \t" << received << " pixels, hash " << received_hash << "\n"
	     << link << " Check \t\t : \t" << (in_order ? "PASSED (received as sent, in the sent order)" : "FAILED") << endl;
	return in_order;
}

// S1: smoothens the input image <runs> times. In a run whose result is passed on, every pixel (or batch
// of pixels) is written to the pipe as soon as it is ready.
void S1_process()
{
	int width = input_image->width, height = input_image->height;
	int number_of_pixels = width * height;
	uint8_t *smoothened_pixels = new uint8_t[number_of_pixels * 3];
	uint64_t sent = 0, hash = HASH_START;

	for (int run = 1; run <= runs; run++)
	{
		bool pass_result = (run % runs_per_result == 0);
		int pixel = 0;
		int pending = 0;				// pixels that are ready but not sent yet

		for (int i = 0; i < height; i++)
		{
			for (int j = 0; j < width; j++)
			{
				S1_smoothen_pixel(input_image, i, j, smoothened_pixels + pixel * 3);
				pixel++;

				if (pass_result)
				{
					pending++;
					if (pending == batch || pixel == number_of_pixels)
					{
						uint8_t *message = smoothened_pixels + (pixel - pending) * 3;
						hash = hash_pixels(hash, message, pending);
						send_bytes(pipe_1[1], message, pending * 3);
						sent += pending;
						pending = 0;
					}
				}
			}
		}
	}

	uint64_t summary[2] = {sent, hash};
	send_bytes(pipe_1[1], summary, sizeof(summary));
	close(pipe_1[1]);

	cout << "S1 Finished After \t : \t" << seconds_since(t_processing) << " s" << endl;
}

// S2: finds the details. The first run on a result of S1 reads the smoothened pixels from the pipe while S1
// is still producing them; the other runs on the same result (variant 2) use the copy that S2 has kept.
// Returns true if the pixels from S1 were received as sent.
bool S2_process()
{
	int width = input_image->width, height = input_image->height;
	int number_of_pixels = width * height;
	uint8_t *smoothened_pixels = new uint8_t[number_of_pixels * 3];
	uint8_t *details_pixels = new uint8_t[number_of_pixels * 3];
	uint64_t received = 0, received_hash = HASH_START;
	uint64_t sent = 0, hash = HASH_START;

	for (int run = 1; run <= runs; run++)
	{
		bool receive_result = ((run - 1) % runs_per_result == 0);
		bool pass_result = (run % runs_per_result == 0);
		int pixel = 0;
		int available = receive_result ? 0 : number_of_pixels;	// smoothened pixels that S2 already has
		int pending = 0;

		for (int i = 0; i < height; i++)
		{
			for (int j = 0; j < width; j++)
			{
				if (pixel == available)
				{
					int count = min(batch, number_of_pixels - pixel);
					receive_bytes(pipe_1[0], smoothened_pixels + pixel * 3, count * 3);
					received_hash = hash_pixels(received_hash, smoothened_pixels + pixel * 3, count);
					received += count;
					available += count;
				}

				S2_find_details_pixel(input_image->image_pixels[i][j], smoothened_pixels + pixel * 3, details_pixels + pixel * 3);
				pixel++;

				if (pass_result)
				{
					pending++;
					if (pending == batch || pixel == number_of_pixels)
					{
						uint8_t *message = details_pixels + (pixel - pending) * 3;
						hash = hash_pixels(hash, message, pending);
						send_bytes(pipe_2[1], message, pending * 3);
						sent += pending;
						pending = 0;
					}
				}
			}
		}
	}

	uint64_t summary_of_S1[2];
	receive_bytes(pipe_1[0], summary_of_S1, sizeof(summary_of_S1));
	close(pipe_1[0]);

	uint64_t summary[2] = {sent, hash};
	send_bytes(pipe_2[1], summary, sizeof(summary));
	close(pipe_2[1]);

	bool in_order = check_link("S1 -> S2", received, received_hash, summary_of_S1[0], summary_of_S1[1]);
	cout << "S2 Finished After \t : \t" << seconds_since(t_processing) << " s" << endl;
	return in_order;
}

// S3: sharpens, and writes the result of its last run to the file.
// Returns true if the pixels from S2 were received as sent.
bool S3_process(char *path_to_output_file)
{
	int width = input_image->width, height = input_image->height;
	int number_of_pixels = width * height;
	uint8_t *details_pixels = new uint8_t[number_of_pixels * 3];
	struct image_t *sharpened_image = allocate_image(width, height);
	uint64_t received = 0, received_hash = HASH_START;

	for (int run = 1; run <= runs; run++)
	{
		bool receive_result = ((run - 1) % runs_per_result == 0);
		int pixel = 0;
		int available = receive_result ? 0 : number_of_pixels;	// details pixels that S3 already has

		for (int i = 0; i < height; i++)
		{
			for (int j = 0; j < width; j++)
			{
				if (pixel == available)
				{
					int count = min(batch, number_of_pixels - pixel);
					receive_bytes(pipe_2[0], details_pixels + pixel * 3, count * 3);
					received_hash = hash_pixels(received_hash, details_pixels + pixel * 3, count);
					received += count;
					available += count;
				}

				S3_sharpen_pixel(input_image->image_pixels[i][j], details_pixels + pixel * 3, sharpened_image->image_pixels[i][j]);
				pixel++;
			}
		}
	}

	uint64_t summary_of_S2[2];
	receive_bytes(pipe_2[0], summary_of_S2, sizeof(summary_of_S2));
	close(pipe_2[0]);

	double processing_seconds = seconds_since(t_processing);
	const auto t_processed = chrono::steady_clock::now();

	write_ppm_file(path_to_output_file, sharpened_image);

	double write_seconds = seconds_since(t_processed);

	bool in_order = check_link("S2 -> S3", received, received_hash, summary_of_S2[0], summary_of_S2[1]);
	cout << "S3 Finished After \t : \t" << processing_seconds << " s\n"
	     << "Processing Time \t : \t" << processing_seconds << " s\n"
	     << "File Write Time \t : \t" << write_seconds << " s" << endl;
	return in_order;
}

int main(int argc, char **argv)
{
	if(argc < 4 || argc > 6)
	{
		cout << "usage: ./part2_2.out <path-to-original-image> <path-to-transformed-image> <variant> [runs] [batch]\n\n";
		exit(0);
	}

	variant = atoi(argv[3]);
	runs = (argc > 4) ? atoi(argv[4]) : 1000;
	batch = (argc > 5) ? atoi(argv[5]) : 1;
	if (variant != 1 && variant != 2)
	{
		cerr << "The variant must be 1 or 2" << endl;
		return 1;
	}
	runs_per_result = (variant == 1) ? 1 : 100;
	if (runs < 1 || runs % runs_per_result != 0)
	{
		cerr << "The number of runs must be at least 1, and a multiple of 100 in variant 2" << endl;
		return 1;
	}
	if (batch < 1)
	{
		cerr << "The batch must be at least 1 pixel" << endl;
		return 1;
	}

	const auto t_start = chrono::steady_clock::now();

	input_image = read_ppm_file(argv[1]);

	double read_seconds = seconds_since(t_start);

	int number_of_pixels = input_image->width * input_image->height;
	if (batch > number_of_pixels) batch = number_of_pixels;

	cout << "Image \t\t\t : \t" << argv[1] << " (" << input_image->width << " x " << input_image->height << ")" << endl;
	cout << "Variant \t\t : \t" << variant << endl;
	cout << "Runs \t\t\t : \t" << runs << endl;
	cout << "Batch \t\t\t : \t" << batch << " pixel(s) per message" << endl;
	cout << "File Read Time \t\t : \t" << read_seconds << " s" << endl;

	if (pipe(pipe_1) == -1 || pipe(pipe_2) == -1)
	{
		perror("pipe");
		return 1;
	}

	t_processing = chrono::steady_clock::now();

	// Every child closes the pipe ends it does not use. Otherwise a reader would never see the end of
	// its pipe, because a write end would still be open somewhere.
	pid_t s1_pid = fork();
	if (s1_pid == 0)
	{
		close(pipe_1[0]);
		close(pipe_2[0]);
		close(pipe_2[1]);
		S1_process();
		return 0;
	}

	pid_t s2_pid = fork();
	if (s2_pid == 0)
	{
		close(pipe_1[1]);
		close(pipe_2[0]);
		return S2_process() ? 0 : 1;
	}

	pid_t s3_pid = fork();
	if (s3_pid == 0)
	{
		close(pipe_1[0]);
		close(pipe_1[1]);
		close(pipe_2[1]);
		return S3_process(argv[2]) ? 0 : 1;
	}

	if (s1_pid < 0 || s2_pid < 0 || s3_pid < 0)
	{
		perror("fork");
		return 1;
	}

	close(pipe_1[0]);
	close(pipe_1[1]);
	close(pipe_2[0]);
	close(pipe_2[1]);

	bool all_passed = true;
	for (int i = 0; i < 3; i++)
	{
		int child_status;
		wait(&child_status);
		if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) all_passed = false;
	}

	cout << "Total Time \t\t : \t" << seconds_since(t_start) << " s" << endl;

	return all_passed ? 0 : 1;
}
