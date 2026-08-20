// lab 1 > src > image_sharpener.cpp

#include <iostream>
#include "libppm.h"
#include <cstdint>
#include <chrono>
#include <fstream>
#include <string>

using namespace std;

struct image_t* S1_smoothen(struct image_t *input_image)
{
	// TODO
	image_t *smoothened_image = new struct image_t;
	smoothened_image->width = input_image->width;
	smoothened_image->height = input_image->height;
	smoothened_image->image_pixels = new uint8_t**[smoothened_image->height];
	for (int i = 0; i < input_image->height; i++)
	{
		smoothened_image->image_pixels[i] = new uint8_t*[smoothened_image->width];
		for (int j = 0; j < input_image->width; j++)
		{
			smoothened_image->image_pixels[i][j] = new uint8_t[3];
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
			smoothened_image->image_pixels[i][j][0] = sum[0] / count;
			smoothened_image->image_pixels[i][j][1] = sum[1] / count;
			smoothened_image->image_pixels[i][j][2] = sum[2] / count;
		}
	}
	// remember to allocate space for smoothened_image. See read_ppm_file() in libppm.c for some help.
	return smoothened_image;
}

struct image_t* S2_find_details(struct image_t *input_image, struct image_t *smoothened_image)
{
	// TODO
	image_t *details_image = new struct image_t;
	details_image->width = input_image->width;
	details_image->height = input_image->height;
	details_image->image_pixels = new uint8_t**[details_image->height];
	for (int i = 0; i < details_image->height; i++) {
		details_image->image_pixels[i] = new uint8_t*[details_image->width];
		for (int j = 0; j < details_image->width; j++) details_image->image_pixels[i][j] = new uint8_t[3];
	}

	for (int i = 0; i < input_image->height; i++) {
		for (int j = 0; j < input_image->width; j++) {
			details_image->image_pixels[i][j][0] = (input_image->image_pixels[i][j][0] > smoothened_image->image_pixels[i][j][0])? (input_image->image_pixels[i][j][0] - smoothened_image->image_pixels[i][j][0]) : 0;
			details_image->image_pixels[i][j][1] = (input_image->image_pixels[i][j][1] > smoothened_image->image_pixels[i][j][1])? (input_image->image_pixels[i][j][1] - smoothened_image->image_pixels[i][j][1]) : 0;
			details_image->image_pixels[i][j][2] = (input_image->image_pixels[i][j][2] > smoothened_image->image_pixels[i][j][2])? (input_image->image_pixels[i][j][2] - smoothened_image->image_pixels[i][j][2]) : 0;
		}
	}
	return details_image;
}

struct image_t* S3_sharpen(struct image_t *input_image, struct image_t *details_image)
{
	// TODO
	image_t *sharpened_image = new struct image_t;
	sharpened_image->width = input_image->width;
	sharpened_image->height = input_image->height;
	sharpened_image->image_pixels = new uint8_t**[sharpened_image->height];
	for (int i = 0; i < sharpened_image->height; i++) {
		sharpened_image->image_pixels[i] = new uint8_t*[sharpened_image->width];
		for (int j = 0; j < sharpened_image->width; j++) sharpened_image->image_pixels[i][j] = new uint8_t[3];
	}

	for (int i = 0; i < input_image->height; i++) {
		for (int j = 0; j < input_image->width; j++) {
			sharpened_image->image_pixels[i][j][0] = min(255, input_image->image_pixels[i][j][0] + details_image->image_pixels[i][j][0]);
			sharpened_image->image_pixels[i][j][1] = min(255, input_image->image_pixels[i][j][1] + details_image->image_pixels[i][j][1]);
			sharpened_image->image_pixels[i][j][2] = min(255, input_image->image_pixels[i][j][2] + details_image->image_pixels[i][j][2]);
		}
	}
	return sharpened_image;
}

int main(int argc, char **argv)
{
	if(argc != 3)
	{
		cout << "usage: ./a.out <path-to-original-image> <path-to-transformed-image>\n\n";
		exit(0);
	}

	const auto start = chrono::high_resolution_clock::now();
	
	struct image_t *input_image = read_ppm_file(argv[1]);
	
	struct image_t *smoothened_image = S1_smoothen(input_image);
	
	struct image_t *details_image = S2_find_details(input_image, smoothened_image);
	
	struct image_t *sharpened_image = S3_sharpen(input_image, details_image);
	
	const auto end = chrono::high_resolution_clock::now();
	const auto duration = chrono::duration_cast<chrono::microseconds>(end - start);

	string file_number = string(argv[1]).substr(string(argv[1]).find_last_of('/') + 1, string(argv[1]).find_last_of('.') - string(argv[1]).find_last_of('/') - 1);

	cout << "Time taken to sharpen the image " << file_number << ": " << duration.count() << " microseconds\n";
	ofstream timing_file("timing.txt", ios::app);
	timing_file << "Time taken to sharpen the image " << file_number << ": " << duration.count() << " microseconds\n";
	timing_file.close();

	write_ppm_file(argv[2], sharpened_image);
	
	return 0;
}
