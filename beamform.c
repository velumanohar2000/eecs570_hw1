// 3D Ultrasound beamforming baseline code for EECS 570 
// Created by: Richard Sampson, Amlan Nayak, Thomas F. Wenisch
// Revision 1.0 - 11/15/16

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <pthread.h>

#define NUM_THREADS_REFLECT 16
#define NUM_THREADS_TRANSMIT 2
#define NUM_THREADS_X 8


typedef struct thread_args{
    int start;
    int end;
	float *image_temp;
}thread_args;

typedef struct args_divide_x{
    int start;
    int end;
	int it_rx;
	int offset;
	float *image_temp;
}args_divide_x;

int sls_t; // Number of scanlines in theta
int sls_p;
int pts_r = 1560; // Radial points along scanline

float tx_x = 0; // Transmit transducer x position
float tx_y = 0; // Transmit transducer y position
float tx_z = -0.001; // Transmit transducer z position

float *point_x; // Point x position
float *point_y; // Point y position
float *point_z; // Point z position

float *dist_tx; // Transmit distance (ie first leg only)


int trans_x = 32; // Transducers in x dim
int trans_y = 32; // Transducers in y dim

float *image;  // Pointer to full image (accumulated so far)

float *rx_x; // Receive transducer x position
float *rx_y; // Receive transducer y position
float rx_z = 0; // Receive transducer z position


const float idx_const = 0.000009625; // Speed of sound and sampling rate, converts dist to index
const int filter_delay = 140; // Constant added to index to account filter delay (off by 1 from MATLAB)

int data_len = 12308; // Number for pre-processed data values per channel
float *rx_data; // Pointer to pre-processed receive channel data

int size;

int total_angles;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// with 8 threads we are able to double performace for transmit distance
void *transmit_distance(void *arg){

	thread_args *thread_info = (struct thread_args *) arg;

	/* First compute transmit distance */
	int point;
	int it_t; // Iterator for theta
	int it_p; // Iterator for phi
	int it_r; // Iterator for r

	/* Variables for distance calculation and index conversion */
	float x_comp; // Itermediate value for dist calc
	float y_comp; // Itermediate value for dist calc
	float z_comp; // Itermediate value for dist calc
	int it_angle;

	point = thread_info->start * pts_r;

	for(it_angle = thread_info->start; it_angle < thread_info->end; it_angle++) {
		for (it_r = 0; it_r < pts_r; it_r++) {

			x_comp = tx_x - point_x[point];
			x_comp = x_comp * x_comp;
			y_comp = tx_y - point_y[point];
			y_comp = y_comp * y_comp;
			z_comp = tx_z - point_z[point];
			z_comp = z_comp * z_comp;

			dist_tx[point++] = (float)sqrt(x_comp + y_comp + z_comp);
		}
	}
}

void *divide_x_image(void *arg){
	args_divide_x *thread_info = (struct args_divide_x *) arg;

	int it_t; // Iterator for theta
	int it_p; // Iterator for phi
	int it_r; // Iterator for r
	int index; // Index into transducer data
	float x_comp; // Itermediate value for dist calc
	float y_comp; // Itermediate value for dist calc
	float z_comp; // Itermediate value for dist calc
	float dist;

	int it_rx = thread_info->it_rx; // Iterator for recieve transducer
	int offset = thread_info->offset;

	int point = thread_info->start * sls_p * pts_r;
	float *image_pos = thread_info->image_temp + thread_info->start * sls_p * pts_r;


	for (it_t = thread_info->start; it_t < thread_info->end; it_t++) {    // whatever size is
		for (it_p = 0; it_p < sls_p; it_p++) { // whatever size is
			for (it_r = 0; it_r < pts_r; it_r++) { //1056
				
				x_comp = rx_x[it_rx] - point_x[point];
				x_comp = x_comp * x_comp;
				y_comp = rx_y[it_rx] - point_y[point];
				y_comp = y_comp * y_comp;
				z_comp = rx_z - point_z[point];
				z_comp = z_comp * z_comp;


				dist = dist_tx[point++] + (float)sqrt(x_comp + y_comp + z_comp); // change to local
				index = (int)(dist/idx_const + filter_delay + 0.5);
				*image_pos += rx_data[index+offset];
				image_pos++;
			}
		}
	}
}



void *reflect_distance(void *arg){
/* Now compute reflected distance, find index values, add to image */

	thread_args *thread_info = (struct thread_args *) arg;

	int point = 0;
	int it_rx; // Iterator for recieve transducer
	int it_t; // Iterator for theta
	int it_p; // Iterator for phi
	int it_r; // Iterator for r
	int index; // Index into transducer data
	float x_comp; // Itermediate value for dist calc
	float y_comp; // Itermediate value for dist calc
	float z_comp; // Itermediate value for dist calc
	float dist;
	int offset = 0;
	float *image_pos; // Pointer to current position in image
	float *image_temp = thread_info->image_temp;

	offset = thread_info->start * data_len;
	for (it_rx = thread_info->start; it_rx < thread_info->end; it_rx++) {  // 1024 times

		//image_pos = image; // Reset image pointer back to beginning
		point = 0;
		image_temp = thread_info->image_temp;

		args_divide_x divide_x_args[NUM_THREADS_X];
		pthread_t child_reflect_x[NUM_THREADS_X];

		int current_start, range;
		int i = 0;

		current_start = 0;
		range = sls_t / NUM_THREADS_X;
		for(i = 0; i < NUM_THREADS_X; i++) {
			divide_x_args[i].start = current_start;
			divide_x_args[i].end = current_start + range;
			divide_x_args[i].it_rx = it_rx;
			divide_x_args[i].offset = offset;
			divide_x_args[i].image_temp = image_temp;
			current_start += range;

		}
		divide_x_args[NUM_THREADS_X-1].end = sls_t;

		for(i = 0; i < NUM_THREADS_X; i++) {
        	pthread_create(&child_reflect_x[i], NULL, divide_x_image, &divide_x_args[i]);
    	}
    	for(i = 0; i < NUM_THREADS_X; i++) {
        	pthread_join(child_reflect_x[i], NULL);
    	}
		offset += data_len;
	}

}


void allocate_space()
{
	
	/* Allocate space for data */
	rx_x = (float*) malloc(trans_x * trans_y * sizeof(float));
	if (rx_x == NULL) fprintf(stderr, "Bad malloc on rx_x\n");
	rx_y = (float*) malloc(trans_x * trans_y * sizeof(float));
	if (rx_y == NULL) fprintf(stderr, "Bad malloc on rx_y\n");
	rx_data = (float*) malloc(data_len * trans_x * trans_y * sizeof(float));
	if (rx_data == NULL) fprintf(stderr, "Bad malloc on rx_data\n");

	point_x = (float *) malloc(pts_r * sls_t * sls_p * sizeof(float));
	if (point_x == NULL) fprintf(stderr, "Bad malloc on point_x\n");
	point_y = (float *) malloc(pts_r * sls_t * sls_p * sizeof(float));
	if (point_y == NULL) fprintf(stderr, "Bad malloc on point_y\n");
	point_z = (float *) malloc(pts_r * sls_t * sls_p * sizeof(float));
	if (point_z == NULL) fprintf(stderr, "Bad malloc on point_z\n");

	dist_tx = (float*) malloc(pts_r * sls_t * sls_p * sizeof(float));
	if (dist_tx == NULL) fprintf(stderr, "Bad malloc on dist_tx\n");

	image = (float *) malloc(pts_r * sls_t * sls_p * sizeof(float));
	if (image == NULL) fprintf(stderr, "Bad malloc on image\n");
	memset(image, 0, pts_r * sls_t * sls_p * sizeof(float));

}

void read_binary(FILE *input)
{
	/* Load data from binary */
	fread(rx_x, sizeof(float), trans_x * trans_y, input); 
	fread(rx_y, sizeof(float), trans_x * trans_y, input); 

	fread(point_x, sizeof(float), pts_r * sls_t * sls_p, input); 
	fread(point_y, sizeof(float), pts_r * sls_t * sls_p, input); 
	fread(point_z, sizeof(float), pts_r * sls_t * sls_p, input); 

	fread(rx_data, sizeof(float), data_len * trans_x * trans_y, input); 
	fclose(input);
}

int main (int argc, char **argv) {

	size = atoi(argv[1]);



	/* Variables for image space points */
	sls_t = size; // Number of scanlines in theta
	sls_p = size; // Number of scanlines in phi
	total_angles = sls_p * sls_t;

	allocate_space();


    FILE* input;
    FILE* output;

	// read cmd line input
		if (argc < 1 || !(strcmp(argv[1],"16") || strcmp(argv[1],"32") || strcmp(argv[1],"64"))) {
				printf("Usage: %s {16|32|64}\n",argv[0]);
				fflush(stdout);
				exit(-1);
		}
		char buff[128];
			#ifdef __MIC__
		sprintf(buff, "/beamforming_input_%s.bin", argv[1]);
			#else // !__MIC__
		sprintf(buff, "/n/typhon/data1/home/eecs570/beamforming_input_%s.bin", argv[1]);
			#endif
		input = fopen(buff,"rb");
		if (!input) {
			printf("Unable to open input file %s.\n", buff);
			fflush(stdout);
			exit(-1);
		}	
	//

	read_binary(input);


	printf("Beginning computation\n");
	fflush(stdout);

	
	
	
	

	

    int current_start, range;
	int i = 0;
	int j = 0;

	// Transmit thread init
	pthread_t transmit_threads[NUM_THREADS_TRANSMIT];
	thread_args transmit_work_ranges[NUM_THREADS_TRANSMIT];

    current_start = 0;
    range = total_angles / NUM_THREADS_TRANSMIT;
    for(i = 0; i < NUM_THREADS_TRANSMIT; i++) {
        transmit_work_ranges[i].start = current_start;
        transmit_work_ranges[i].end = current_start + range;
        current_start += range;
    }
    transmit_work_ranges[NUM_THREADS_TRANSMIT-1].end = total_angles;

	// Reflect Thread init
	pthread_t reflect_threads[NUM_THREADS_REFLECT];
	thread_args reflect_work_ranges[NUM_THREADS_REFLECT];
	float **temp_images = (float **)malloc(NUM_THREADS_REFLECT * sizeof(float *));


	current_start = 0;
    range = 1024 / NUM_THREADS_REFLECT;
    for(i = 0; i < NUM_THREADS_REFLECT; i++) {
        reflect_work_ranges[i].start = current_start;
        reflect_work_ranges[i].end = current_start + range;

		temp_images[i] = (float *)malloc(pts_r * sls_t * sls_p * sizeof(float));
		if (temp_images[i] == NULL) fprintf(stderr, "Bad malloc on temp_images[%d]\n", i);
		memset(temp_images[i], 0, pts_r * sls_t * sls_p * sizeof(float));

		reflect_work_ranges[i].image_temp = temp_images[i];

		current_start += range;
    }
    reflect_work_ranges[NUM_THREADS_REFLECT-1].end = 1024;

	
	




	/* get start timestamp */
 	struct timeval tv;
	gettimeofday(&tv,NULL);
	uint64_t start = tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
	
	/* --------------------------- COMPUTATION ------------------------------ */
	for(i = 0; i < NUM_THREADS_TRANSMIT; i++) {
        pthread_create(&transmit_threads[i], NULL, transmit_distance, &transmit_work_ranges[i]);
    }
    for(i = 0; i < NUM_THREADS_TRANSMIT; i++) {
        pthread_join(transmit_threads[i], NULL);
    }


	gettimeofday(&tv,NULL);
    uint64_t end_transmit = tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
    uint64_t elapsed_transmit = end_transmit - start;


	for(i = 0; i < NUM_THREADS_REFLECT; i++) {
        pthread_create(&reflect_threads[i], NULL, reflect_distance, &reflect_work_ranges[i]);
    }
    for(i = 0; i < NUM_THREADS_REFLECT; i++) {
        pthread_join(reflect_threads[i], NULL);
    }

	    // Combine temporary images into the final image
    for (i = 0; i < NUM_THREADS_REFLECT; i++) {
        for (j = 0; j < pts_r * sls_t * sls_p; j++) {
            image[j] += temp_images[i][j];
        }
        free(temp_images[i]);
    }
	free(temp_images);

	

	/* --------------------------------------------------------------------- */

	/* get elapsed time */
    	gettimeofday(&tv,NULL);
    	uint64_t end = tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
    	uint64_t elapsed = end - start;

	printf("Transmit time (usec): %lld\n", elapsed_transmit);
	printf("Reflect time (usec): %lld\n", end - end_transmit);
	printf("@@@ Elapsed time (usec): %lld\n", elapsed);
	printf("Processing complete.  Preparing output.\n");
	fflush(stdout);

	/* Write result to file */
	char* out_filename;
        #ifdef __MIC__
	  out_filename = "/home/micuser/beamforming_output.bin";
        #else // !__MIC__
	  out_filename = "beamforming_output.bin";
        #endif
        output = fopen(out_filename,"wb");
	fwrite(image, sizeof(float), pts_r * sls_t * sls_p, output); 
	fclose(output);

	printf("Output complete.\n");
	fflush(stdout);

	/* Cleanup */
	free(rx_x);
	free(rx_y);
	free(rx_data);
	free(point_x);
	free(point_y);
	free(point_z);
	free(dist_tx);
	free(image);

	return 0;
}
