// lab 5 > src > part1 > part1.cpp

// Part I: baseline application without any parallelism.
// This is the image sharpener of Lab 1. Two things are changed so that it can be the baseline of the other
// parts: the transformation (S1, S2, S3) is repeated <runs> times on the image that is read only once, and
// the three result images are allocated once before the runs instead of inside S1, S2 and S3 on every call.
// The work done on one pixel is the same as in Lab 1; it is only moved into the three *_pixel functions,
// which the other parts use as well.

#include <iostream>
#include "../libppm.h"
#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <algorithm>

using namespace std;

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

void S1_smoothen(struct image_t *input_image, struct image_t *smoothened_image)
{
	for (int i = 0; i < input_image->height; i++)
	{
		for (int j = 0; j < input_image->width; j++)
		{
			S1_smoothen_pixel(input_image, i, j, smoothened_image->image_pixels[i][j]);
		}
	}
}

void S2_find_details(struct image_t *input_image, struct image_t *smoothened_image, struct image_t *details_image)
{
	for (int i = 0; i < input_image->height; i++)
	{
		for (int j = 0; j < input_image->width; j++)
		{
			S2_find_details_pixel(input_image->image_pixels[i][j], smoothened_image->image_pixels[i][j], details_image->image_pixels[i][j]);
		}
	}
}

void S3_sharpen(struct image_t *input_image, struct image_t *details_image, struct image_t *sharpened_image)
{
	for (int i = 0; i < input_image->height; i++)
	{
		for (int j = 0; j < input_image->width; j++)
		{
			S3_sharpen_pixel(input_image->image_pixels[i][j], details_image->image_pixels[i][j], sharpened_image->image_pixels[i][j]);
		}
	}
}

double seconds_between(chrono::steady_clock::time_point from, chrono::steady_clock::time_point to)
{
	return chrono::duration<double>(to - from).count();
}

int main(int argc, char **argv)
{
	if(argc < 3 || argc > 4)
	{
		cout << "usage: ./part1.out <path-to-original-image> <path-to-transformed-image> [runs]\n\n";
		exit(0);
	}

	int runs = (argc == 4) ? atoi(argv[3]) : 1000;
	if (runs < 1)
	{
		cerr << "The number of runs must be at least 1" << endl;
		return 1;
	}

	const auto t_start = chrono::steady_clock::now();

	struct image_t *input_image = read_ppm_file(argv[1]);

	const auto t_read = chrono::steady_clock::now();

	struct image_t *smoothened_image = allocate_image(input_image->width, input_image->height);
	struct image_t *details_image = allocate_image(input_image->width, input_image->height);
	struct image_t *sharpened_image = allocate_image(input_image->width, input_image->height);

	double s1_seconds = 0, s2_seconds = 0, s3_seconds = 0;

	const auto t_processing = chrono::steady_clock::now();

	for (int run = 1; run <= runs; run++)
	{
		const auto t0 = chrono::steady_clock::now();

		S1_smoothen(input_image, smoothened_image);

		const auto t1 = chrono::steady_clock::now();

		S2_find_details(input_image, smoothened_image, details_image);

		const auto t2 = chrono::steady_clock::now();

		S3_sharpen(input_image, details_image, sharpened_image);

		const auto t3 = chrono::steady_clock::now();

		s1_seconds += seconds_between(t0, t1);
		s2_seconds += seconds_between(t1, t2);
		s3_seconds += seconds_between(t2, t3);
	}

	const auto t_processed = chrono::steady_clock::now();

	write_ppm_file(argv[2], sharpened_image);

	const auto t_end = chrono::steady_clock::now();

	cout << "Image \t\t\t : \t" << argv[1] << " (" << input_image->width << " x " << input_image->height << ")" << endl;
	cout << "Runs \t\t\t : \t" << runs << endl;
	cout << "File Read Time \t\t : \t" << seconds_between(t_start, t_read) << " s" << endl;
	cout << "S1 Time \t\t : \t" << s1_seconds << " s" << endl;
	cout << "S2 Time \t\t : \t" << s2_seconds << " s" << endl;
	cout << "S3 Time \t\t : \t" << s3_seconds << " s" << endl;
	cout << "Processing Time \t : \t" << seconds_between(t_processing, t_processed) << " s" << endl;
	cout << "File Write Time \t : \t" << seconds_between(t_processed, t_end) << " s" << endl;
	cout << "Total Time \t\t : \t" << seconds_between(t_start, t_end) << " s" << endl;

	return 0;
}
