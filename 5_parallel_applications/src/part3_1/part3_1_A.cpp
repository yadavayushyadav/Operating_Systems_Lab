// lab 5 > src > part3_1 > part3_1_A.cpp

// Part III.1, computer A: S1 and S2 run on this computer, S3 runs on computer B.
// Computer A is the server: it reads the image, waits on a port for computer B to connect, and then sends
//   1. a header (width, height, variant, runs, batch),
//   2. the input image, once (S3 needs it, and the image file is read only here),
//   3. the details pixels, in row-major order, in messages of <batch> pixels, as S2 gets them ready,
//   4. the number of pixels sent and their hash, so that computer B can check what it received.
// At the end computer B answers with one byte that says whether its check passed.
// The socket is a TCP socket (SOCK_STREAM): bytes arrive in the order they were sent and none are lost.
//
// Variant 1: S1 and S2 are run once per result, and every result is sent.
// Variant 2: S1 is run 100 times, S2 is run 100 times on the result of the 100th run of S1, and the result
//            of the 100th run of S2 is sent.

#include <iostream>
#include "../libppm.h"
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <unistd.h>
#include <signal.h>
#include <endian.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace std;

const uint64_t HASH_START = 1469598103934665603ULL;
const uint64_t HASH_PRIME = 1099511628211ULL;

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

// write() may write fewer bytes than asked, so it is repeated until all are written
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

// read() returns as soon as some bytes have arrived, so it is repeated until the whole message is there
void receive_bytes(int fd, void *bytes, size_t size)
{
	size_t done = 0;
	while (done < size)
	{
		ssize_t received = read(fd, (uint8_t *) bytes + done, size - done);
		if (received <= 0)
		{
			if (received == 0) cerr << "The other computer closed the connection" << endl;
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

// Waits on the port for computer B and returns the socket of the connection (as in the example of the
// getaddrinfo man page, but with a TCP socket, so there is a listen() and an accept())
int wait_for_computer_B(char *port)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		// IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;	// TCP
	hints.ai_flags = AI_PASSIVE;		// wildcard address: accept a connection on any network interface

	int s = getaddrinfo(NULL, port, &hints, &result);
	if (s != 0)
	{
		cerr << "getaddrinfo: " << gai_strerror(s) << endl;
		exit(1);
	}

	// getaddrinfo() returns a list of addresses; use the first one that can be bound
	int listen_fd = -1;
	for (rp = result; rp != NULL; rp = rp->ai_next)
	{
		listen_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (listen_fd == -1) continue;

		int yes = 1;
		setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));	// the port can be used again right after a run

		if (bind(listen_fd, rp->ai_addr, rp->ai_addrlen) == 0) break;

		close(listen_fd);
	}
	freeaddrinfo(result);

	if (rp == NULL)
	{
		cerr << "Could not bind to port " << port << endl;
		exit(1);
	}
	if (listen(listen_fd, 1) == -1)
	{
		perror("listen");
		exit(1);
	}

	cout << "Waiting for computer B on port " << port << " ..." << endl;

	struct sockaddr_storage peer_address;
	socklen_t peer_address_length = sizeof(peer_address);
	int socket_fd = accept(listen_fd, (struct sockaddr *) &peer_address, &peer_address_length);
	if (socket_fd == -1)
	{
		perror("accept");
		exit(1);
	}
	close(listen_fd);

	char host[NI_MAXHOST];
	if (getnameinfo((struct sockaddr *) &peer_address, peer_address_length, host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == 0)
		cout << "Computer B connected from " << host << endl;

	return socket_fd;
}

int main(int argc, char **argv)
{
	if(argc < 4 || argc > 6)
	{
		cout << "usage: ./part3_1_A.out <path-to-original-image> <port> <variant> [runs] [batch]\n\n";
		exit(0);
	}

	char *port = argv[2];
	int variant = atoi(argv[3]);
	int runs = (argc > 4) ? atoi(argv[4]) : 1000;
	int batch = (argc > 5) ? atoi(argv[5]) : 1;
	if (variant != 1 && variant != 2)
	{
		cerr << "The variant must be 1 or 2" << endl;
		return 1;
	}
	int runs_per_result = (variant == 1) ? 1 : 100;
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

	signal(SIGPIPE, SIG_IGN);			// if computer B goes away, write() fails with an error instead of killing us

	const auto t_start = chrono::steady_clock::now();

	struct image_t *input_image = read_ppm_file(argv[1]);

	double read_seconds = seconds_since(t_start);

	int width = input_image->width, height = input_image->height;
	int number_of_pixels = width * height;
	if (batch > number_of_pixels) batch = number_of_pixels;

	cout << "Image \t\t\t : \t" << argv[1] << " (" << width << " x " << height << ")" << endl;
	cout << "Variant \t\t : \t" << variant << endl;
	cout << "Runs \t\t\t : \t" << runs << endl;
	cout << "Batch \t\t\t : \t" << batch << " pixel(s) per message" << endl;
	cout << "File Read Time \t\t : \t" << read_seconds << " s" << endl;

	int socket_fd = wait_for_computer_B(port);

	const auto t_processing = chrono::steady_clock::now();

	// the header, in network byte order
	uint32_t header[5] = {htonl(width), htonl(height), htonl(variant), htonl(runs), htonl(batch)};
	send_bytes(socket_fd, header, sizeof(header));

	// the input image, as one block of number_of_pixels * 3 bytes
	uint8_t *input_pixels = new uint8_t[number_of_pixels * 3];
	for (int i = 0; i < height; i++)
	{
		for (int j = 0; j < width; j++)
		{
			memcpy(input_pixels + (i * width + j) * 3, input_image->image_pixels[i][j], 3);
		}
	}
	send_bytes(socket_fd, input_pixels, number_of_pixels * 3);

	uint8_t *smoothened_pixels = new uint8_t[number_of_pixels * 3];
	uint8_t *details_pixels = new uint8_t[number_of_pixels * 3];
	uint64_t sent = 0, hash = HASH_START;

	for (int result = 1; result <= runs / runs_per_result; result++)
	{
		// S1: only the result of its last run is used by S2
		for (int run = 1; run <= runs_per_result; run++)
		{
			int pixel = 0;
			for (int i = 0; i < height; i++)
			{
				for (int j = 0; j < width; j++)
				{
					S1_smoothen_pixel(input_image, i, j, smoothened_pixels + pixel * 3);
					pixel++;
				}
			}
		}

		// S2: in its last run every pixel (or batch of pixels) is sent to computer B as soon as it is ready
		for (int run = 1; run <= runs_per_result; run++)
		{
			bool pass_result = (run == runs_per_result);
			int pixel = 0;
			int pending = 0;			// pixels that are ready but not sent yet

			for (int i = 0; i < height; i++)
			{
				for (int j = 0; j < width; j++)
				{
					S2_find_details_pixel(input_image->image_pixels[i][j], smoothened_pixels + pixel * 3, details_pixels + pixel * 3);
					pixel++;

					if (pass_result)
					{
						pending++;
						if (pending == batch || pixel == number_of_pixels)
						{
							uint8_t *message = details_pixels + (pixel - pending) * 3;
							hash = hash_pixels(hash, message, pending);
							send_bytes(socket_fd, message, pending * 3);
							sent += pending;
							pending = 0;
						}
					}
				}
			}
		}
	}

	uint64_t summary[2] = {htobe64(sent), htobe64(hash)};
	send_bytes(socket_fd, summary, sizeof(summary));

	double finished_after = seconds_since(t_processing);

	// computer B answers when S3 has finished its last run
	uint8_t check_passed;
	receive_bytes(socket_fd, &check_passed, 1);

	double processing_seconds = seconds_since(t_processing);
	close(socket_fd);

	cout << "A -> B Sent \t\t : \t" << sent << " pixels, hash " << hash << endl;
	cout << "A -> B Check \t\t : \t" << (check_passed ? "PASSED (computer B received them as sent, in the sent order)" : "FAILED") << endl;
	cout << "A Finished After \t : \t" << finished_after << " s" << endl;
	cout << "Processing Time \t : \t" << processing_seconds << " s" << endl;
	cout << "Total Time \t\t : \t" << seconds_since(t_start) << " s (includes waiting for computer B to connect)" << endl;

	return check_passed ? 0 : 1;
}
