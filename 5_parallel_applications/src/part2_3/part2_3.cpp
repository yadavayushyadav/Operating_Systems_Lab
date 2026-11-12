// lab 5 > src > part2_3 > part2_3.cpp

// Part II.3: S1, S2 and S3 are three different processes that communicate via shared memory.
// The parent reads the image once, creates a POSIX shared memory object that holds two queues (S1 -> S2 and
// S2 -> S3), maps it, and forks one child per stage. The children inherit the mapping, so all of them see
// the same queues. The synchronization is done with semaphores that live in the shared memory:
//   empty_slots counts the free slots of a queue  (the sender waits on it while the queue is full)
//   full_slots  counts the messages in the queue  (the receiver waits on it while the queue is empty)
// A queue has exactly one sender and one receiver. head is used only by the receiver and tail only by the
// sender, so the two counting semaphores are enough and no mutex is needed.
//
// Pixels are passed in row-major order, in messages of <batch> pixels. By default batch = 1, i.e. every
// pixel is passed as soon as it is ready.
// Variant 1: every run of a stage passes its result to the next stage.
// Variant 2: only every 100th run of S1 passes its result. S2 runs 100 times on every result it receives
//            and passes the result of the 100th run, and S3 does the same.

#include <iostream>
#include "../libppm.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>

using namespace std;

const int QUEUE_SIZE = 64;				// number of messages a queue can hold
const uint64_t HASH_START = 1469598103934665603ULL;
const uint64_t HASH_PRIME = 1099511628211ULL;

// Lives in the shared memory. alignas(64) gives every queue cache lines of its own, so that the processes
// that work on one queue do not slow down the process that works only on the other one (false sharing).
struct alignas(64) queue_t
{
	sem_t empty_slots;					// free slots
	sem_t full_slots;					// messages waiting in the queue
	int head;							// slot of the message that is received next (used only by the receiver)
	int tail;							// slot for the message that is sent next (used only by the sender)
	uint8_t *messages;					// QUEUE_SIZE slots of (batch * 3) bytes each, also in the shared memory.
										// The memory is mapped before fork(), so this address is valid in every process.

	// written by the sender and by the receiver when they have finished, for the proof
	uint64_t pixels_sent, hash_sent;
	uint64_t pixels_received, hash_received;
};

struct image_t *input_image;
int runs, variant, batch;
int runs_per_result;					// runs of a stage for every result it passes on: 1 in variant 1, 100 in variant 2

struct queue_t *queue_1;				// S1 -> S2
struct queue_t *queue_2;				// S2 -> S3

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

// Waits for a free slot, copies the message into it, and tells the receiver that one more message is there
void send_pixels(struct queue_t *queue, uint8_t *pixels, int number_of_pixels)
{
	sem_wait(&queue->empty_slots);
	memcpy(queue->messages + (size_t) queue->tail * batch * 3, pixels, number_of_pixels * 3);
	queue->tail = (queue->tail + 1) % QUEUE_SIZE;
	sem_post(&queue->full_slots);
}

// Waits for a message, copies it out, and tells the sender that one more slot is free
void receive_pixels(struct queue_t *queue, uint8_t *pixels, int number_of_pixels)
{
	sem_wait(&queue->full_slots);
	memcpy(pixels, queue->messages + (size_t) queue->head * batch * 3, number_of_pixels * 3);
	queue->head = (queue->head + 1) % QUEUE_SIZE;
	sem_post(&queue->empty_slots);
}

double seconds_since(chrono::steady_clock::time_point from)
{
	return chrono::duration<double>(chrono::steady_clock::now() - from).count();
}

// S1: smoothens the input image <runs> times. In a run whose result is passed on, every pixel (or batch
// of pixels) is put into the queue as soon as it is ready.
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
						send_pixels(queue_1, message, pending);
						sent += pending;
						pending = 0;
					}
				}
			}
		}
	}

	queue_1->pixels_sent = sent;
	queue_1->hash_sent = hash;

	cout << "S1 Finished After \t : \t" << seconds_since(t_processing) << " s" << endl;
}

// S2: finds the details. The first run on a result of S1 takes the smoothened pixels from the queue while S1
// is still producing them; the other runs on the same result (variant 2) use the copy that S2 has kept.
void S2_process()
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
					receive_pixels(queue_1, smoothened_pixels + pixel * 3, count);
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
						send_pixels(queue_2, message, pending);
						sent += pending;
						pending = 0;
					}
				}
			}
		}
	}

	queue_1->pixels_received = received;
	queue_1->hash_received = received_hash;
	queue_2->pixels_sent = sent;
	queue_2->hash_sent = hash;

	cout << "S2 Finished After \t : \t" << seconds_since(t_processing) << " s" << endl;
}

// S3: sharpens, and writes the result of its last run to the file.
void S3_process(char *path_to_output_file)
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
					receive_pixels(queue_2, details_pixels + pixel * 3, count);
					received_hash = hash_pixels(received_hash, details_pixels + pixel * 3, count);
					received += count;
					available += count;
				}

				S3_sharpen_pixel(input_image->image_pixels[i][j], details_pixels + pixel * 3, sharpened_image->image_pixels[i][j]);
				pixel++;
			}
		}
	}

	queue_2->pixels_received = received;
	queue_2->hash_received = received_hash;

	double processing_seconds = seconds_since(t_processing);
	const auto t_processed = chrono::steady_clock::now();

	write_ppm_file(path_to_output_file, sharpened_image);

	double write_seconds = seconds_since(t_processed);

	cout << "S3 Finished After \t : \t" << processing_seconds << " s\n"
	     << "Processing Time \t : \t" << processing_seconds << " s\n"
	     << "File Write Time \t : \t" << write_seconds << " s" << endl;
}

// The children have written what they sent and received into the shared memory; the parent compares them
bool check_link(const char *link, struct queue_t *queue)
{
	bool in_order = (queue->pixels_sent == queue->pixels_received && queue->hash_sent == queue->hash_received);
	cout << link << " Sent \t\t : \t" << queue->pixels_sent << " pixels, hash " << queue->hash_sent << "\n"
	     << link << " Received \t : \t" << queue->pixels_received << " pixels, hash " << queue->hash_received << "\n"
	     << link << " Check \t\t : \t" << (in_order ? "PASSED (received as sent, in the sent order)" : "FAILED") << endl;
	return in_order;
}

int main(int argc, char **argv)
{
	if(argc < 4 || argc > 6)
	{
		cout << "usage: ./part2_3.out <path-to-original-image> <path-to-transformed-image> <variant> [runs] [batch]\n\n";
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

	// Shared memory: the two queues, followed by the message slots of queue 1 and of queue 2
	size_t slots_size = (size_t) QUEUE_SIZE * batch * 3;
	size_t shared_size = 2 * sizeof(struct queue_t) + 2 * slots_size;
	string shared_name = "/lab5_part2_3_" + to_string(getpid());

	int shared_fd = shm_open(shared_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
	if (shared_fd == -1)
	{
		perror("shm_open");
		return 1;
	}
	if (ftruncate(shared_fd, shared_size) == -1)
	{
		perror("ftruncate");
		shm_unlink(shared_name.c_str());
		return 1;
	}
	void *shared_memory = mmap(NULL, shared_size, PROT_READ | PROT_WRITE, MAP_SHARED, shared_fd, 0);
	if (shared_memory == MAP_FAILED)
	{
		perror("mmap");
		shm_unlink(shared_name.c_str());
		return 1;
	}
	close(shared_fd);						// the mapping stays after the descriptor is closed
	shm_unlink(shared_name.c_str());		// and after the name is removed, until every process has unmapped it

	queue_1 = (struct queue_t *) shared_memory;
	queue_2 = queue_1 + 1;
	queue_1->messages = (uint8_t *) (queue_2 + 1);
	queue_2->messages = queue_1->messages + slots_size;
	queue_1->head = queue_1->tail = 0;
	queue_2->head = queue_2->tail = 0;

	// the second argument (1) says that the semaphore is shared between processes, not between threads
	sem_init(&queue_1->empty_slots, 1, QUEUE_SIZE);
	sem_init(&queue_1->full_slots, 1, 0);
	sem_init(&queue_2->empty_slots, 1, QUEUE_SIZE);
	sem_init(&queue_2->full_slots, 1, 0);

	t_processing = chrono::steady_clock::now();

	pid_t s1_pid = fork();
	if (s1_pid == 0)
	{
		S1_process();
		return 0;
	}

	pid_t s2_pid = fork();
	if (s2_pid == 0)
	{
		S2_process();
		return 0;
	}

	pid_t s3_pid = fork();
	if (s3_pid == 0)
	{
		S3_process(argv[2]);
		return 0;
	}

	if (s1_pid < 0 || s2_pid < 0 || s3_pid < 0)
	{
		perror("fork");
		return 1;
	}

	bool all_finished = true;
	for (int i = 0; i < 3; i++)
	{
		int child_status;
		wait(&child_status);
		if (!WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0) all_finished = false;
	}

	bool in_order_1 = check_link("S1 -> S2", queue_1);
	bool in_order_2 = check_link("S2 -> S3", queue_2);

	sem_destroy(&queue_1->empty_slots);
	sem_destroy(&queue_1->full_slots);
	sem_destroy(&queue_2->empty_slots);
	sem_destroy(&queue_2->full_slots);
	munmap(shared_memory, shared_size);

	cout << "Total Time \t\t : \t" << seconds_since(t_start) << " s" << endl;

	return (all_finished && in_order_1 && in_order_2) ? 0 : 1;
}
