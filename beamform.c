// 3D Ultrasound beamforming baseline code for EECS 570 
// Created by: Richard Sampson, Amlan Nayak, Thomas F. Wenisch
// Revision 1.0 - 11/15/16

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>

#define NUM_THREADS 32

/* Variables for transducer geometry */
int trans_x = 32; // Transducers in x dim
int trans_y = 32; // Transducers in y dim
int data_len = 12308; // Number for pre-processed data values per channel

float tx_x = 0; // Transmit transducer x position
float tx_y = 0; // Transmit transducer y position
float tx_z = -0.001; // Transmit transducer z position
float rx_z = 0; // Receive transducer z position


int sls_t; // Number of scanlines in theta
int sls_p;
int pts_r = 1560; // Radial points along scanline
int total_points;      // pts_r * sls_t * sls_p

const float idx_const = 0.000009625; // Speed of sound and sampling rate, converts dist to index
const int filter_delay = 140; // Constant added to index to account filter delay (off by 1 from MATLAB)

float *rx_x; // Receive transducer x position
float *rx_y; // Receive transducer y position
float *rx_data;  // Pointer to pre-processed receive channel data

float *point_x; // Point x position
float *point_y; // Point y position
float *point_z; // Point z position


float *dist_tx; // Transmit distance (ie first leg only)
float *image;  // Pointer to full image (accumulated so far)

int total_channels; //trans_x * trans_y

pthread_barrier_t barrier;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;


typedef struct {
    int start;
    int end;    
} thread_arg;

/* First compute transmit distance */
/* Second compute reflected distance, find index values, add to image */
void *worker(void *arg) {
    thread_arg *range = (thread_arg *) arg;
    int point;
    int it_rx;
    float x_comp; // Itermediate value for dist calc
	float y_comp; // Itermediate value for dist calc
	float z_comp; // Itermediate value for dist calc

    for (point = range->start; point < range->end; point++) {
        x_comp = tx_x - point_x[point];
        y_comp = tx_y - point_y[point];
        z_comp = tx_z - point_z[point];
        dist_tx[point] = (float)sqrt(x_comp * x_comp + y_comp * y_comp + z_comp * z_comp);
    }
    
    // Wait for all threads to finish transmit
    pthread_barrier_wait(&barrier);
  
    for (point = range->start; point < range->end; point++) {
        float sum = 0; // reset sum
        for (it_rx = 0; it_rx < total_channels; it_rx++) {
            int offset = it_rx * data_len;  
            x_comp = rx_x[it_rx] - point_x[point];
            y_comp = rx_y[it_rx] - point_y[point];
            z_comp = rx_z - point_z[point];
            float rx_d = (float)sqrt(x_comp * x_comp + y_comp * y_comp + z_comp * z_comp);
            float dist = dist_tx[point] + rx_d;
            int index = (int)(dist / idx_const + filter_delay + 0.5);
            sum += rx_data[index + offset];
        }
        image[point] = sum;
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

void read_binary(FILE *input, char* str)
{
    
    char buff[128];
    #ifdef __MIC__
        sprintf(buff, "/beamforming_input_%s.bin", str);
    #else // !__MIC__
        sprintf(buff, "/n/typhon/data1/home/eecs570/beamforming_input_%s.bin", str);
    #endif

    input = fopen(buff,"rb");
    if (!input) {
        printf("Unable to open input file %s.\n", buff);
        fflush(stdout);
        exit(-1);
    }	
	/* Load data from binary */
	fread(rx_x, sizeof(float), trans_x * trans_y, input); 
	fread(rx_y, sizeof(float), trans_x * trans_y, input); 

	fread(point_x, sizeof(float), pts_r * sls_t * sls_p, input); 
	fread(point_y, sizeof(float), pts_r * sls_t * sls_p, input); 
	fread(point_z, sizeof(float), pts_r * sls_t * sls_p, input); 

	fread(rx_data, sizeof(float), data_len * trans_x * trans_y, input); 
	fclose(input);
}

void write_output(FILE* output)
{
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
}

void clean_up()
{
    free(rx_x);
    free(rx_y);
    free(rx_data);
    free(point_x);
    free(point_y);
    free(point_z);
    free(dist_tx);
    free(image);
}

int main (int argc, char **argv) {
    FILE* input;
    FILE* output;

	/* validate command line parameter */
	if (argc < 1 || !(strcmp(argv[1],"16") || strcmp(argv[1],"32") || strcmp(argv[1],"64"))) {
        printf("Usage: %s {16|32|64}\n",argv[0]);
        fflush(stdout);
        exit(-1);
    }


    int size = atoi(argv[1]);
    sls_t = size;  
    sls_p = size; 
    total_points = pts_r * sls_t * sls_p;
    total_channels = trans_x * trans_y;

    
    allocate_space();
    read_binary(input, argv[1]);

	printf("Beginning computation\n");
	fflush(stdout);

    
    pthread_barrier_init(&barrier, NULL, NUM_THREADS);
    
	/* get start timestamp */
    struct timeval tv;
	gettimeofday(&tv,NULL);
	uint64_t start = tv.tv_sec*(uint64_t)1000000+tv.tv_usec;
    
    int i;
    pthread_t threads[NUM_THREADS];
    thread_arg targs[NUM_THREADS];
    int range = total_points / NUM_THREADS;

    for (i = 0; i < NUM_THREADS; i++) {
        targs[i].start = i * range;
        targs[i].end = (i == NUM_THREADS - 1) ? total_points : targs[i].start + range;
        pthread_create(&threads[i], NULL, worker, &targs[i]);
    }

    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Destroy the barrier
    pthread_barrier_destroy(&barrier);
    
    gettimeofday(&tv, NULL);
    uint64_t end_time = tv.tv_sec * (uint64_t)1000000 + tv.tv_usec;
    uint64_t elapsed = end_time - start;
    
    printf("@@@ Elapsed time (usec): %llu\n", elapsed);
    printf("Processing complete.  Preparing output.\n");
	fflush(stdout);

    write_output(output);

    clean_up();

    return 0;
}
