// lab 5 > src > part3_1 > part3_1_B.cpp

// Part III.1, computer B: S3 runs on this computer, S1 and S2 run on computer A.
// Computer B is the client: it connects to computer A and receives
//   1. a header (width, height, variant, runs, batch),
//   2. the input image, once,
//   3. the details pixels, in row-major order, in messages of <batch> pixels, as S2 gets them ready,
//   4. the number of pixels that computer A sent and their hash.
// S3 sharpens the pixels as they arrive. At the end computer B checks that it received what was sent, tells
// computer A the outcome with one byte, and writes the sharpened image to the file.
//
// Variant 1: S3 is run once on every result it receives.
// Variant 2: S3 is run 100 times on every result it receives.

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

// Connects to computer A and returns the socket of the connection (as in the example of the getaddrinfo
// man page, but with a TCP socket). If computer A is not waiting yet, it tries again for a while.
int connect_to_computer_A(char *host, char *port)
{
	struct addrinfo hints;
	struct addrinfo *result, *rp;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		// IPv4 or IPv6
	hints.ai_socktype = SOCK_STREAM;	// TCP

	int s = getaddrinfo(host, port, &hints, &result);
	if (s != 0)
	{
		cerr << "getaddrinfo: " << gai_strerror(s) << endl;
		exit(1);
	}

	for (int attempt = 1; attempt <= 30; attempt++)
	{
		// getaddrinfo() returns a list of addresses; use the first one that accepts the connection
		for (rp = result; rp != NULL; rp = rp->ai_next)
		{
			int socket_fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
			if (socket_fd == -1) continue;

			if (connect(socket_fd, rp->ai_addr, rp->ai_addrlen) != -1)
			{
				freeaddrinfo(result);
				cout << "Connected to computer A at " << host << ", port " << port << endl;
				return socket_fd;
			}

			close(socket_fd);
		}

		if (attempt == 1) cout << "Computer A is not waiting on " << host << ", port " << port << " yet, trying again ..." << endl;
		sleep(1);
	}

	cerr << "Could not connect to computer A. Start the _A target on computer A first, and check the address, the port and the firewall." << endl;
	exit(1);
}

int main(int argc, char **argv)
{
	if(argc != 5)
	{
		cout << "usage: ./part3_1_B.out <address-of-computer-A> <port> <path-to-transformed-image> <variant>\n\n";
		exit(0);
	}

	signal(SIGPIPE, SIG_IGN);			// if computer A goes away, write() fails with an error instead of killing us

	const auto t_start = chrono::steady_clock::now();

	int socket_fd = connect_to_computer_A(argv[1], argv[2]);

	const auto t_processing = chrono::steady_clock::now();

	uint32_t header[5];
	receive_bytes(socket_fd, header, sizeof(header));
	int width = ntohl(header[0]), height = ntohl(header[1]);
	int variant = ntohl(header[2]), runs = ntohl(header[3]), batch = ntohl(header[4]);
	int runs_per_result = (variant == 1) ? 1 : 100;
	int number_of_pixels = width * height;

	if (variant != atoi(argv[4]))
	{
		cerr << "Computer A runs variant " << variant << ", but this is variant " << argv[4] << ". Use the same variant on both computers." << endl;
		return 1;
	}

	uint8_t *input_pixels = new uint8_t[number_of_pixels * 3];
	receive_bytes(socket_fd, input_pixels, number_of_pixels * 3);

	cout << "Image \t\t\t : \t" << width << " x " << height << " (received from computer A)" << endl;
	cout << "Variant \t\t : \t" << variant << endl;
	cout << "Runs \t\t\t : \t" << runs << endl;
	cout << "Batch \t\t\t : \t" << batch << " pixel(s) per message" << endl;

	uint8_t *details_pixels = new uint8_t[number_of_pixels * 3];
	struct image_t *sharpened_image = allocate_image(width, height);
	uint64_t received = 0, received_hash = HASH_START;

	// S3: the first run on a result receives the details pixels while computer A is still producing them;
	// the other runs on the same result (variant 2) use the copy that S3 has kept.
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
					receive_bytes(socket_fd, details_pixels + pixel * 3, count * 3);
					received_hash = hash_pixels(received_hash, details_pixels + pixel * 3, count);
					received += count;
					available += count;
				}

				S3_sharpen_pixel(input_pixels + pixel * 3, details_pixels + pixel * 3, sharpened_image->image_pixels[i][j]);
				pixel++;
			}
		}
	}

	uint64_t summary[2];
	receive_bytes(socket_fd, summary, sizeof(summary));
	uint64_t sent = be64toh(summary[0]), sent_hash = be64toh(summary[1]);

	uint8_t check_passed = (sent == received && sent_hash == received_hash) ? 1 : 0;
	send_bytes(socket_fd, &check_passed, 1);
	close(socket_fd);

	double processing_seconds = seconds_since(t_processing);
	const auto t_processed = chrono::steady_clock::now();

	write_ppm_file(argv[3], sharpened_image);

	double write_seconds = seconds_since(t_processed);

	cout << "A -> B Sent \t\t : \t" << sent << " pixels, hash " << sent_hash << endl;
	cout << "A -> B Received \t : \t" << received << " pixels, hash " << received_hash << endl;
	cout << "A -> B Check \t\t : \t" << (check_passed ? "PASSED (received as sent, in the sent order)" : "FAILED") << endl;
	cout << "Processing Time \t : \t" << processing_seconds << " s" << endl;
	cout << "File Write Time \t : \t" << write_seconds << " s" << endl;
	cout << "Total Time \t\t : \t" << seconds_since(t_start) << " s" << endl;

	return check_passed ? 0 : 1;
}
