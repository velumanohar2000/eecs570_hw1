// 3D Ultrasound beamforming baseline code for EECS 570 
// Created by: Richard Sampson, Amlan Nayak, Thomas F. Wenisch
// Revised to use barriers between two phases for improved speed.
// In this version, a fixed set of threads first compute the transmit
// distances for their assigned image points, then wait at a barrier,
// then use the same threads to compute the reflect contributions.
// This avoids repeatedly spawning threads for the inner loop.
//
// Compile with: gcc -O3 -pthread -o beamformer beamformer.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sys/time.h>
#include <pthread.h>

// Tuning parameter: number of persistent threads
#define NUM_THREADS 32

// Global parameters (from the original code)
int trans_x = 32;      // Number of receive transducers in x dim
int trans_y = 32;      // Number of receive transducers in y dim
int data_len = 12308;  // Number of pre-processed data values per channel

// Transmit transducer positions:
float tx_x = 0;
float tx_y = 0;
float tx_z = -0.001;

// Receive transducer z position:
float rx_z = 0;

// Image space parameters:
int pts_r = 1560;      // Radial points along each scanline
int sls_t;             // Number of scanlines in theta (set via argv)
int sls_p;             // Number of scanlines in phi (set via argv)
int total_points;      // = pts_r * sls_t * sls_p

// Conversion parameters:
const float idx_const = 0.000009625;
const int filter_delay = 140;

// Global arrays (allocated as 1D contiguous arrays):
float *rx_x;      // Size: trans_x * trans_y
float *rx_y;      // Size: trans_x * trans_y
float *rx_data;   // Size: data_len * trans_x * trans_y

float *point_x;   // Size: total_points
float *point_y;   // Size: total_points
float *point_z;   // Size: total_points

float *dist_tx;   // Array to hold transmit distances, size: total_points
float *image;     // Final image, size: total_points

// Barrier to synchronize the two phases.
pthread_barrier_t barrier;

// Structure for thread arguments.
typedef struct {
    int tid;    // Thread id
} thread_arg;

// Worker function that computes two phases.
// Phase 1: Compute transmit distance for assigned image points.
// Phase 2: Compute reflect contributions for the same image points.
void *worker(void *arg) {
    thread_arg *targ = (thread_arg *) arg;
    int tid = targ->tid;
    int i, ch;
    int total_channels = trans_x * trans_y;
    int chunk = total_points / NUM_THREADS;
    int start = tid * chunk;
    int end = (tid == NUM_THREADS - 1) ? total_points : start + chunk;
    
    // Phase 1: Compute transmit distance for each image point in [start, end)
    for (i = start; i < end; i++) {
        float dx = tx_x - point_x[i];
        float dy = tx_y - point_y[i];
        float dz = tx_z - point_z[i];
        dist_tx[i] = sqrtf(dx * dx + dy * dy + dz * dz);
    }
    
    // Synchronize: Wait for all threads to finish Phase 1.
    pthread_barrier_wait(&barrier);
    
    // Phase 2: For each image point in [start, end), compute the reflect contribution.
    for (i = start; i < end; i++) {
        float sum = 0.0f;
        for (ch = 0; ch < total_channels; ch++) {
            int offset = ch * data_len;  // Channel-specific offset in rx_data
            float rx_dx = rx_x[ch] - point_x[i];
            float rx_dy = rx_y[ch] - point_y[i];
            float rx_dz = rx_z - point_z[i];
            float rx_d = sqrtf(rx_dx * rx_dx + rx_dy * rx_dy + rx_dz * rx_dz);
            float d = dist_tx[i] + rx_d;
            int index = (int)(d / idx_const + filter_delay + 0.5f);
            sum += rx_data[index + offset];
        }
        image[i] = sum;
    }
    
    return NULL;
}

int main (int argc, char **argv) {
    int i, j;
    
    // Validate command-line parameter.
    if (argc < 2) {
        printf("Usage: %s {16|32|64}\n", argv[0]);
        return -1;
    }
    int size = atoi(argv[1]);
    if (!(size == 16 || size == 32 || size == 64)) {
        printf("Usage: %s {16|32|64}\n", argv[0]);
        return -1;
    }
    sls_t = size;  // Number of scanlines in theta.
    sls_p = size;  // Number of scanlines in phi.
    total_points = pts_r * sls_t * sls_p;
    
    // Allocate space for data arrays.
    rx_x    = (float *) malloc(trans_x * trans_y * sizeof(float));
    rx_y    = (float *) malloc(trans_x * trans_y * sizeof(float));
    rx_data = (float *) malloc(data_len * trans_x * trans_y * sizeof(float));
    point_x = (float *) malloc(total_points * sizeof(float));
    point_y = (float *) malloc(total_points * sizeof(float));
    point_z = (float *) malloc(total_points * sizeof(float));
    dist_tx = (float *) malloc(total_points * sizeof(float));
    image   = (float *) malloc(total_points * sizeof(float));
    if (!rx_x || !rx_y || !rx_data || !point_x || !point_y || !point_z || !dist_tx || !image) {
        fprintf(stderr, "Memory allocation error\n");
        return -1;
    }
    memset(image, 0, total_points * sizeof(float));
    
    // Read binary input (same as original code).
    FILE *input;
    char buff[128];
#ifdef __MIC__
    sprintf(buff, "/beamforming_input_%s.bin", argv[1]);
#else
    sprintf(buff, "/n/typhon/data1/home/eecs570/beamforming_input_%s.bin", argv[1]);
#endif
    input = fopen(buff, "rb");
    if (!input) {
        printf("Unable to open input file %s.\n", buff);
        return -1;
    }
    fread(rx_x, sizeof(float), trans_x * trans_y, input);
    fread(rx_y, sizeof(float), trans_x * trans_y, input);
    fread(point_x, sizeof(float), total_points, input);
    fread(point_y, sizeof(float), total_points, input);
    fread(point_z, sizeof(float), total_points, input);
    fread(rx_data, sizeof(float), data_len * trans_x * trans_y, input);
    fclose(input);

    printf("Beginning computation\n");
	fflush(stdout);
    
    pthread_barrier_init(&barrier, NULL, NUM_THREADS);
    
    struct timeval tv;
    uint64_t start_time, end_time, elapsed;
    gettimeofday(&tv, NULL);
    start_time = tv.tv_sec * (uint64_t)1000000 + tv.tv_usec;
    
    // Create persistent threads to perform both phases.
    pthread_t threads[NUM_THREADS];
    thread_arg targs[NUM_THREADS];
    for (i = 0; i < NUM_THREADS; i++) {
        targs[i].tid = i;
        pthread_create(&threads[i], NULL, worker, &targs[i]);
    }
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Destroy the barrier.
    pthread_barrier_destroy(&barrier);
    
    gettimeofday(&tv, NULL);
    end_time = tv.tv_sec * (uint64_t)1000000 + tv.tv_usec;
    elapsed = end_time - start_time;
    
    printf("@@@ Elapsed time (usec): %llu\n", (unsigned long long)elapsed);
    
    // Write output to file.
    FILE *output;
    char *out_filename;
#ifdef __MIC__
    out_filename = "/home/micuser/beamforming_output.bin";
#else
    out_filename = "beamforming_output.bin";
#endif
    output = fopen(out_filename, "wb");
    fwrite(image, sizeof(float), total_points, output);
    fclose(output);
    
    // Cleanup.
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
