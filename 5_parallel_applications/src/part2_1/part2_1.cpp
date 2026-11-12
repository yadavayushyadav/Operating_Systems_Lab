// lab 5 > src > part2_1 > part2_1.cpp

// Part II.1: S1, S2 and S3 are three threads of the same process.
// The threads communicate through the address space of the process: S1 passes the smoothened pixels to S2
// through a queue, and S2 passes the details pixels to S3 through a second queue. A queue is protected by
// a spin lock made of an atomic flag (test_and_set), so all the synchronization is done with atomic operations.
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
#include <cstring>
#include <chrono>
#include <algorithm>
#include <thread>
#include <atomic>

using namespace std;

const int QUEUE_SIZE = 64;				// number of messages a queue can hold
const uint64_t HASH_START = 1469598103934665603ULL;
const uint64_t HASH_PRIME = 1099511628211ULL;

// A thread that waits for a message keeps writing the lock flag, so every queue gets a cache line (64 bytes)
// of its own. Without alignas the lock shares its line with the read-only globals below (input_image, batch,
// runs ...), and S1 then loses that line on every pixel it computes (false sharing).
struct alignas(64) queue_t
{
	uint8_t *messages;					// QUEUE_SIZE slots of (batch * 3) bytes each
	int head;							// slot of the message that is received next
	int tail;							// slot for the message that is sent next
	int count;							// messages waiting in the queue
	atomic_flag lock = ATOMIC_FLAG_INIT;	// spin lock for the fields above
};

struct image_t *input_image;
struct image_t *sharpened_image;
int runs, variant, batch;
int runs_per_result;					// runs of a stage for every result it passes on: 1 in variant 1, 100 in variant 2

struct queue_t queue_1;					// S1 -> S2
struct queue_t queue_2;					// S2 -> S3

// For the proof that the pixels are received as sent, in the sent order: [0] is S1 -> S2, [1] is S2 -> S3
uint64_t pixels_sent[2], pixels_received[2];
uint64_t hash_sent[2], hash_received[2];

chrono::steady_clock::time_point t_processing;
double finished_after[3];				// seconds after which S1, S2 and S3 finished

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

// Puts one message into the queue. test_and_set() sets the flag and returns its old value, so it returns
// false only for the thread that changed the flag from clear to set: that thread holds the lock.
// If the queue is full, the lock is released and taken again until the receiver has made room.
void send_pixels(struct queue_t *queue, uint8_t *pixels, int number_of_pixels)
{
	while (true)
	{
		while (queue->lock.test_and_set()) {}
		if (queue->count < QUEUE_SIZE)
		{
			memcpy(queue->messages + (size_t) queue->tail * batch * 3, pixels, number_of_pixels * 3);
			queue->tail = (queue->tail + 1) % QUEUE_SIZE;
			queue->count++;
			queue->lock.clear();
			return;
		}
		queue->lock.clear();
	}
}

// Takes the oldest message out of the queue. If the queue is empty, the lock is released and taken again
// until the sender has put a message.
void receive_pixels(struct queue_t *queue, uint8_t *pixels, int number_of_pixels)
{
	while (true)
	{
		while (queue->lock.test_and_set()) {}
		if (queue->count > 0)
		{
			memcpy(pixels, queue->messages + (size_t) queue->head * batch * 3, number_of_pixels * 3);
			queue->head = (queue->head + 1) % QUEUE_SIZE;
			queue->count--;
			queue->lock.clear();
			return;
		}
		queue->lock.clear();
	}
}

// S1: smoothens the input image <runs> times. In a run whose result is passed on, every pixel (or batch
// of pixels) is sent to S2 as soon as it is ready.
void S1_thread()
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
						send_pixels(&queue_1, message, pending);
						sent += pending;
						pending = 0;
					}
				}
			}
		}
	}

	pixels_sent[0] = sent;
	hash_sent[0] = hash;
	finished_after[0] = chrono::duration<double>(chrono::steady_clock::now() - t_processing).count();
	delete[] smoothened_pixels;
}

// S2: finds the details. The first run on a result of S1 receives the smoothened pixels while S1 is still
// producing them; the other runs on the same result (variant 2) use the copy that S2 has kept.
void S2_thread()
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
					receive_pixels(&queue_1, smoothened_pixels + pixel * 3, count);
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
						send_pixels(&queue_2, message, pending);
						sent += pending;
						pending = 0;
					}
				}
			}
		}
	}

	pixels_received[0] = received;
	hash_received[0] = received_hash;
	pixels_sent[1] = sent;
	hash_sent[1] = hash;
	finished_after[1] = chrono::duration<double>(chrono::steady_clock::now() - t_processing).count();
	delete[] smoothened_pixels;
	delete[] details_pixels;
}

// S3: sharpens. The result of its last run stays in sharpened_image and is written to the file.
void S3_thread()
{
	int width = input_image->width, height = input_image->height;
	int number_of_pixels = width * height;
	uint8_t *details_pixels = new uint8_t[number_of_pixels * 3];
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
					receive_pixels(&queue_2, details_pixels + pixel * 3, count);
					received_hash = hash_pixels(received_hash, details_pixels + pixel * 3, count);
					received += count;
					available += count;
				}

				S3_sharpen_pixel(input_image->image_pixels[i][j], details_pixels + pixel * 3, sharpened_image->image_pixels[i][j]);
				pixel++;
			}
		}
	}

	pixels_received[1] = received;
	hash_received[1] = received_hash;
	finished_after[2] = chrono::duration<double>(chrono::steady_clock::now() - t_processing).count();
	delete[] details_pixels;
}

double seconds_between(chrono::steady_clock::time_point from, chrono::steady_clock::time_point to)
{
	return chrono::duration<double>(to - from).count();
}

int main(int argc, char **argv)
{
	if(argc < 4 || argc > 6)
	{
		cout << "usage: ./part2_1.out <path-to-original-image> <path-to-transformed-image> <variant> [runs] [batch]\n\n";
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

	const auto t_read = chrono::steady_clock::now();

	int number_of_pixels = input_image->width * input_image->height;
	if (batch > number_of_pixels) batch = number_of_pixels;

	sharpened_image = allocate_image(input_image->width, input_image->height);
	queue_1.messages = new uint8_t[(size_t) QUEUE_SIZE * batch * 3];
	queue_2.messages = new uint8_t[(size_t) QUEUE_SIZE * batch * 3];
	queue_1.head = queue_1.tail = queue_1.count = 0;
	queue_2.head = queue_2.tail = queue_2.count = 0;

	t_processing = chrono::steady_clock::now();

	thread s1(S1_thread);
	thread s2(S2_thread);
	thread s3(S3_thread);
	s1.join();
	s2.join();
	s3.join();

	const auto t_processed = chrono::steady_clock::now();

	write_ppm_file(argv[2], sharpened_image);

	const auto t_end = chrono::steady_clock::now();

	bool in_order[2];
	for (int link = 0; link < 2; link++)
	{
		in_order[link] = (pixels_sent[link] == pixels_received[link] && hash_sent[link] == hash_received[link]);
	}

	cout << "Image \t\t\t : \t" << argv[1] << " (" << input_image->width << " x " << input_image->height << ")" << endl;
	cout << "Variant \t\t : \t" << variant << endl;
	cout << "Runs \t\t\t : \t" << runs << endl;
	cout << "Batch \t\t\t : \t" << batch << " pixel(s) per message" << endl;
	cout << "S1 -> S2 Sent \t\t : \t" << pixels_sent[0] << " pixels, hash " << hash_sent[0] << endl;
	cout << "S1 -> S2 Received \t : \t" << pixels_received[0] << " pixels, hash " << hash_received[0] << endl;
	cout << "S1 -> S2 Check \t\t : \t" << (in_order[0] ? "PASSED (received as sent, in the sent order)" : "FAILED") << endl;
	cout << "S2 -> S3 Sent \t\t : \t" << pixels_sent[1] << " pixels, hash " << hash_sent[1] << endl;
	cout << "S2 -> S3 Received \t : \t" << pixels_received[1] << " pixels, hash " << hash_received[1] << endl;
	cout << "S2 -> S3 Check \t\t : \t" << (in_order[1] ? "PASSED (received as sent, in the sent order)" : "FAILED") << endl;
	cout << "File Read Time \t\t : \t" << seconds_between(t_start, t_read) << " s" << endl;
	cout << "S1 Finished After \t : \t" << finished_after[0] << " s" << endl;
	cout << "S2 Finished After \t : \t" << finished_after[1] << " s" << endl;
	cout << "S3 Finished After \t : \t" << finished_after[2] << " s" << endl;
	cout << "Processing Time \t : \t" << seconds_between(t_processing, t_processed) << " s" << endl;
	cout << "File Write Time \t : \t" << seconds_between(t_processed, t_end) << " s" << endl;
	cout << "Total Time \t\t : \t" << seconds_between(t_start, t_end) << " s" << endl;

	return (in_order[0] && in_order[1]) ? 0 : 1;
}
