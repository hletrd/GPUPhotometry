#ifdef __APPLE__
	#include "/usr/local/Cellar/cfitsio/3.370/include/fitsio.h"
	#include <OpenCL/opencl.h>
#else
	#include "fitsio.h"
	#include <CL/cl.h>
#endif
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#ifdef _WIN32
#else
	#include <sys/time.h>
#endif

#define filecnt_max 100

char nbias[filecnt_max][256], ndark[filecnt_max][256], nflat[filecnt_max][256], nphoto[filecnt_max][256];
fitsfile *bias[filecnt_max];
fitsfile *dark[filecnt_max];
fitsfile *flat[filecnt_max];
fitsfile *photo[filecnt_max];
fitsfile *tmp;

const char *OpenCL_kernel = "\n\
__kernel void average(__global float* input, __global float* output, int xsize, int ysize, int imgsize, int count) { \n\
	int num = get_global_id(0)*xsize; \n\
	for(int i = 0; i < xsize; i++) { \n\
		for(int j = 0; j < count; j++) { \n\
			output[num+i] += input[j*imgsize+num+i]; \n\
		} \n\
		output[num+i] /= count; \n\
	} \n\
} \n\
__kernel void average_dark(__global float* input, __global float* output, __global float* input_bias, int xsize, int ysize, int imgsize, int count) { \n\
	int num = get_global_id(0)*xsize; \n\
	for(int i = 0; i < xsize; i++) { \n\
		for(int j = 0; j < count; j++) { \n\
			output[num+i] += input[j*imgsize+num+i]; \n\
		} \n\
		output[num+i] /= count; \n\
		output[num+i] -= input_bias[num+i]; \n\
	} \n\
}\n\
#define SWAP(a,b) temp=(a);(a)=(b);(b)=temp; \n\
inline float qselect(float *arr, int n, int k) { \n\
	int i,ir,j,l,mid; \n\
	float a,temp; \n\
\n\
	l=0; \n\
	ir=n-1; \n\
	for(;;) { \n\
		if (ir <= l+1) { \n\
			if (ir == l+1 && arr[ir] < arr[l]) { \n\
				SWAP(arr[l],arr[ir]); \n\
			} \n\
			return arr[k]; \n\
		} else { \n\
			mid = (l+ir) >> 1; \n\
			SWAP(arr[mid],arr[l+1]); \n\
			if (arr[l] > arr[ir]) { \n\
				SWAP(arr[l],arr[ir]); \n\
			} \n\
			if (arr[l+1] > arr[ir]) { \n\
				SWAP(arr[l+1],arr[ir]); \n\
			} \n\
			if (arr[l] > arr[l+1]) { \n\
				SWAP(arr[l],arr[l+1]); \n\
			} \n\
			i = l+1; \n\
			j = ir; \n\
			a = arr[l+1]; \n\
			for (;;) { \n\
				do i++; while (arr[i] < a); \n\
				do j--; while (arr[j] > a); \n\
				if (j < i) break; \n\
					SWAP(arr[i],arr[j]); \n\
				} \n\
				arr[l+1] = arr[j]; \n\
				arr[j] = a; \n\
				if (j >= k) ir = j-1; \n\
				if (j <= k) l = i; \n\
		} \n\
	} \n\
} \n\
__kernel void median_flat(__global float* input, __global float* output, __global float* input_dark, __global float* exptime_flat, __global float* input_bias, float exptime_dark, int xsize, int ysize, int imgsize, int count, int mid) { \n\
	int num = get_global_id(0)*xsize; \n\
	float inp[100]; //filecnt_max \n\
	for(int i = 0; i < xsize; i++) { \n\
		for(int j = 0; j < count; j++) { \n\
			inp[j] = input[j*imgsize+num+i] - input_dark[num+i] / exptime_dark * exptime_flat[j]; \n\
		} \n\
		output[num+i] = qselect(inp, count, mid) - input_bias[num+i]; \n\
	} \n\
}\n\
__kernel void photo(__global float* input, __global float* output, __global float* input_bias, __global float* input_dark, __global float* input_flat, __global float* exptime_photo, float exptime_dark, int xsize, int ysize, int imgsize, int count, float flat_avg) { \n\
	int num = get_global_id(0)*xsize; \n\
	for(int i = 0; i < xsize; i++) { \n\
		for(int j = 0; j < count; j++) { \n\
			output[j*imgsize+num+i] = ((input[j*imgsize+num+i] - input_bias[num+i]) - (input_dark[num+i] / exptime_dark * exptime_photo[j])) * flat_avg / input_flat[num+i]; \n\
		} \n\
	} \n\
}";

int main(int argc, char *argv[]) {
	#ifdef _WIN32
	#else
	struct timeval t0;
	gettimeofday(&t0, 0);
	int sec, usec;
	#endif
	FILE *config;
	long imgsize[100];
	int realx = 0, realy = 0;
	int cnt_bias, cnt_dark, cnt_flat, cnt_photo;
	float exptime_dark[filecnt_max], exptime_flat[filecnt_max], exptime_photo[filecnt_max];

	int err;
	size_t global, local;
	cl_device_id device_id;
	cl_context context;
	cl_command_queue commands;
	cl_program program;
	cl_kernel kernel;
	cl_mem input, input_bias, input_dark, input_flat, input_exptime_flat, input_exptime_photo;
	cl_mem output;

	fprintf(stderr, "GPUPhotometry by HLETRD\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Initializing...\n");

	fprintf(stderr, "Reading config...\n");
	if (argc != 2 || access(argv[1], F_OK) != -1) {
		fprintf(stderr, "Running by config: %s\n", argv[1]);
		config = fopen(argv[1], "r");
	} else {
		fprintf(stderr, "config file not found.\n\nUsage: pm <config_filename>\n");
		return 0;
	}

	char device[100];
	fgets(device, 100, config);

	if (device[0] == 'G' || device[0] == 'g') {
		err = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_GPU, 1, &device_id, NULL);
		fprintf(stderr, "Using GPU...\n");
	} else {
		err = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_CPU, 1, &device_id, NULL);
		fprintf(stderr, "Using CPU...\n");
	}
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to initialize OpenCL device group.\n");
		return EXIT_FAILURE;
	}

	char name[100];
	err = clGetDeviceInfo(device_id, CL_DEVICE_NAME, 100, name, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to get OpenCL device info.\n");
		return EXIT_FAILURE;
	} else {
		fprintf(stderr, "OpenCL device detected: %s\n", name);
	}

	context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
	if (!context) {
		fprintf(stderr, "Error: Failed to create a OpenCL compute context.\n");
		return EXIT_FAILURE;
	}

	commands = clCreateCommandQueue(context, device_id, 0, &err);
	if (!commands) {
		fprintf(stderr, "Error: Failed to create a OpenCL commands.\n");
		return EXIT_FAILURE;
	}

	program = clCreateProgramWithSource(context, 1, (const char **)&OpenCL_kernel, NULL, &err);
	if (!program) {
		fprintf(stderr, "Error: Failed to create OpenCL program.\n");
		return EXIT_FAILURE;
	}
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS) {
		size_t len;
		char buffer[2048];

		fprintf(stderr, "Error: Failed to build program executable!\n");
		clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		fprintf(stderr, "%s\n", buffer);
		return EXIT_FAILURE;
	}

	fscanf(config, "%d", &cnt_bias);
	fscanf(config, "%d", &cnt_dark);
	fscanf(config, "%d", &cnt_flat);
	fscanf(config, "%d", &cnt_photo);

	for (int i=0; i<cnt_bias; i++) {
		fscanf(config, "%s", nbias[i]);
	}

	for (int i=0; i<cnt_dark; i++) {
		fscanf(config, "%s", ndark[i]);
	}

	for (int i=0; i<cnt_flat; i++) {
		fscanf(config, "%s", nflat[i]);
	}

	for (int i=0; i<cnt_photo; i++) {
		fscanf(config, "%s", nphoto[i]);
	}
	fclose(config);

	int status;
	fprintf(stderr, "Reading files...\n");
	for (int i=0; i<cnt_bias; i++) {
		fits_open_diskfile(&bias[i], nbias[i], READONLY, &status);
		if (status) {
			fprintf(stderr, "Error reading bias file %s\n", nbias[i]);
			return status;
		}
		fits_get_img_size(bias[i], 2, imgsize, &status);
		fprintf(stderr, "Reading bias: %s\n", nbias[i]);
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			fprintf(stderr, "Error: size of the bias file %s is different with others.\n", nbias[i]);
			return 1;
		}
	}

	for (int i=0; i<cnt_dark; i++) {
		fits_open_diskfile(&dark[i], ndark[i], READONLY, &status);
		if (status) {
			fprintf(stderr, "Error reading dark file %s\n", ndark[i]);
			return status;
		}
		fits_get_img_size(dark[i], 2, imgsize, &status);
		fits_read_key(dark[i], TFLOAT, "EXPTIME", exptime_dark+i, NULL, &status);
		fprintf(stderr, "Reading dark: %s (%.3f sec exposure)\n", ndark[i], exptime_dark[i]);
		if (i>0 && exptime_dark[i] != exptime_dark[i-1]) {
			fprintf(stderr, "Error: exposure time of dark frames are not equal.\n");
			return 1;
		}
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			fprintf(stderr, "Error: size of the dark file %s is different with others.\n", ndark[i]);
			return 1;
		}
	}

	for (int i=0; i<cnt_flat; i++) {
		fits_open_diskfile(&flat[i], nflat[i], READONLY, &status);
		if (status) {
			fprintf(stderr, "Error reading flat file %s\n", nflat[i]);
			return status;
		}
		fits_get_img_size(flat[i], 2, imgsize, &status);
		fits_read_key(flat[i], TFLOAT, "EXPTIME", exptime_flat+i, NULL, &status);
		fprintf(stderr, "Reading flat: %s (%.3f sec exposure)\n", nflat[i], exptime_flat[i]);
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			fprintf(stderr, "Error: size of the flat file %s is different with others.\n", nflat[i]);
			return 1;
		}
	}

	for (int i=0; i<cnt_photo; i++) {
		fits_open_diskfile(&photo[i], nphoto[i], READONLY, &status);
		if (status) {
			fprintf(stderr, "Error reading dark file %s\n", nphoto[i]);
			return status;
		}
		fits_get_img_size(photo[i], 2, imgsize, &status);
		fits_read_key(photo[i], TFLOAT, "EXPTIME", exptime_photo+i, NULL, &status);
		fprintf(stderr, "Reading photo: %s (%.3f sec exposure)\n", nphoto[i], exptime_photo[i]);
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			fprintf(stderr, "Error: size of the photo file %s is different with others.\n", nphoto[i]);
			return 1;
		}
	}

	float *bias_comb = (float*)calloc(realx*realy, sizeof(float));
	float *dark_comb = (float*)calloc(realx*realy, sizeof(float));
	float *flat_comb = (float*)calloc(realx*realy, sizeof(float));
	float *photo_comb = (float*)calloc(realx*realy*cnt_photo, sizeof(float));
	long imgsize_mem = realx * realy;



	float *pixels_single = (float*)malloc(cnt_bias*realx*realy*sizeof(float));
	if (pixels_single == NULL) {
		fprintf(stderr, "Error: failed to allocate %ldbytes of memory.", cnt_bias*realx*realy*sizeof(float));
		return 1;
	}
	long pixeli[2];
	int count;
	int imgsize_mem_int;
	pixeli[0] = 1;
	pixeli[1] = 1;
	int mempos = 0;
	fprintf(stderr, "Loading bias files...\n");
	for(int j=0; j<cnt_bias; j++) {
		fits_read_pix(bias[j], TFLOAT, pixeli, realx*realy, NULL, pixels_single+mempos, NULL, &status);
		mempos += imgsize_mem;
	}

	if (cnt_bias > 0) {
		fprintf(stderr, "Combining bias...\n");
		count = realy;
		kernel = clCreateKernel(program, "average", &err);
		if (!kernel || err != CL_SUCCESS) {
			fprintf(stderr, "Error: Failed to create OpenCL kernel.\n");
			return EXIT_FAILURE;
		}
		input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_bias*realx*realy, NULL, NULL);
		output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*realx*realy, NULL, NULL);
		if (!input || !output) {
			fprintf(stderr, "Error: Failed to allocate OpenCL device memory.\n");
			return EXIT_FAILURE;
		}    
		err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_bias*realx*realy, pixels_single, 0, NULL, NULL);
		if (err != CL_SUCCESS) {
			fprintf(stderr, "Error: Failed to write to OpenCL source array.\n");
			return EXIT_FAILURE;
		}
		imgsize_mem_int = (int)imgsize_mem;
		err = 0;
		err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
		err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
		err |= clSetKernelArg(kernel, 2, sizeof(int), &realx);
		err |= clSetKernelArg(kernel, 3, sizeof(int), &realy);
		err |= clSetKernelArg(kernel, 4, sizeof(int), &imgsize_mem_int);
		err |= clSetKernelArg(kernel, 5, sizeof(int), &cnt_bias);
		if (err != CL_SUCCESS) {
			fprintf(stderr, "Error: Failed to set OpenCL kernel arguments. %d\n", err);
			return EXIT_FAILURE;
		}
		err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
		if (err != CL_SUCCESS) {
			fprintf(stderr, "Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
			return EXIT_FAILURE;
		}
		global = (size_t)count;
		err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
		if (err) {
			fprintf(stderr, "Error: Failed to execute OpenCL kernel.\n");
			return EXIT_FAILURE;
		}
		clFinish(commands);
		err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);  
		if (err != CL_SUCCESS) {
			fprintf(stderr, "Error: Failed to read OpenCL output. %d\n", err);
			exit(1);
		}

		clReleaseMemObject(input);
		clReleaseMemObject(output);
		clReleaseKernel(kernel);
	}

	free(pixels_single);
	pixels_single = (float*)malloc(cnt_dark*realx*realy*sizeof(float));
	if (pixels_single == NULL) {
		fprintf(stderr, "Error: failed to allocate %ldbytes of memory.", cnt_dark*realx*realy*sizeof(float));
		return 1;
	}
	pixeli[0] = 1;
	pixeli[1] = 1;
	mempos = 0;
	fprintf(stderr, "Loading dark files...\n");
	for(int j=0; j<cnt_dark; j++) {
		fits_read_pix(dark[j], TFLOAT, pixeli, realx*realy, NULL, pixels_single+mempos, NULL, &status);
		mempos += imgsize_mem;
	}
	fprintf(stderr, "Combining dark & Subtracting bias...\n");
	count = realy;
	kernel = clCreateKernel(program, "average_dark", &err);
	if (!kernel || err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to create OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_dark*realx*realy, NULL, NULL);
	input_bias = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	if (!input || !output) {
		fprintf(stderr, "Error: Failed to allocate OpenCL device memory.\n");
		return EXIT_FAILURE;
	}    
	err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_dark*realx*realy, pixels_single, 0, NULL, NULL);
	err = clEnqueueWriteBuffer(commands, input_bias, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to write to OpenCL source array.\n");
		return EXIT_FAILURE;
	}
	imgsize_mem_int = (int)imgsize_mem;
	err = 0;
	err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
	err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
	err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &input_bias);
	err |= clSetKernelArg(kernel, 3, sizeof(int), &realx);
	err |= clSetKernelArg(kernel, 4, sizeof(int), &realy);
	err |= clSetKernelArg(kernel, 5, sizeof(int), &imgsize_mem_int);
	err |= clSetKernelArg(kernel, 6, sizeof(int), &cnt_dark);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to set OpenCL kernel arguments. %d\n", err);
		return EXIT_FAILURE;
	}
	err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
		return EXIT_FAILURE;
	}
	global = (size_t)count;
	err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
	if (err) {
		fprintf(stderr, "Error: Failed to execute OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	clFinish(commands);
	err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy, dark_comb, 0, NULL, NULL);  
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to read OpenCL output. %d\n", err);
		exit(1);
	}

	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseMemObject(input_bias);
	clReleaseKernel(kernel);


	free(pixels_single);
	pixels_single = (float*)malloc(cnt_flat*realx*realy*sizeof(float));
	if (pixels_single == NULL) {
		fprintf(stderr, "Error: failed to allocate %ldbytes of memory.", cnt_flat*realx*realy*sizeof(float));
		return 1;
	}
	pixeli[0] = 1;
	pixeli[1] = 1;
	mempos = 0;
	fprintf(stderr, "Loading flat files...\n");
	for(int j=0; j<cnt_flat; j++) {
		for(pixeli[1]=realy; pixeli[1]>0; pixeli[1]--) {
			fits_read_pix(flat[j], TFLOAT, pixeli, realx, NULL, pixels_single+realx*pixeli[1]-realx+mempos, NULL, &status);
		}
		mempos += imgsize_mem;
	}
	count = realy;
	fprintf(stderr, "Combining flat & Subtracting bias, dark...\n");
	kernel = clCreateKernel(program, "median_flat", &err);
	if (!kernel || err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to create OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_flat*realx*realy, NULL, NULL);
	input_dark = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	input_exptime_flat = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_flat, NULL, NULL);
	input_bias = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	if (!input || !input_dark || !output) {
		fprintf(stderr, "Error: Failed to allocate OpenCL device memory.\n");
		return EXIT_FAILURE;
	}    
	err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_flat*realx*realy, pixels_single, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_dark, CL_TRUE, 0, sizeof(float)*realx*realy, dark_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_exptime_flat, CL_TRUE, 0, sizeof(float)*cnt_flat, exptime_flat, 0, NULL, NULL);
	err = clEnqueueWriteBuffer(commands, input_bias, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to write to OpenCL source array.\n");
		return EXIT_FAILURE;
	}
	imgsize_mem_int = (int)imgsize_mem;
	int mid = cnt_flat / 2;
	err = 0;
	err = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
	err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
	err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &input_dark);
	err |= clSetKernelArg(kernel, 3, sizeof(cl_mem), &input_exptime_flat);
	err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &input_bias);
	err |= clSetKernelArg(kernel, 5, sizeof(float), &exptime_dark[0]);
	err |= clSetKernelArg(kernel, 6, sizeof(int), &realx);
	err |= clSetKernelArg(kernel, 7, sizeof(int), &realy);
	err |= clSetKernelArg(kernel, 8, sizeof(int), &imgsize_mem_int);
	err |= clSetKernelArg(kernel, 9, sizeof(int), &cnt_flat);
	err |= clSetKernelArg(kernel, 10, sizeof(int), &mid);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to set OpenCL kernel arguments. %d\n", err);
		return EXIT_FAILURE;
	}
	err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
		return EXIT_FAILURE;
	}
	global = (size_t)count;
	err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
	if (err) {
		fprintf(stderr, "Error: Failed to execute OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	clFinish(commands);
	err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy, flat_comb, 0, NULL, NULL);  
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to read OpenCL output. %d\n", err);
		exit(1);
	}

	double flatsum = 0;
	for(int i=0; i<imgsize_mem_int; i++) {
		flatsum += (double)flat_comb[i];
	}
	float flat_avg = flatsum / imgsize_mem_int;
	free(pixels_single);
	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseMemObject(input_dark);
	clReleaseMemObject(input_exptime_flat);
	clReleaseMemObject(input_bias);
	clReleaseKernel(kernel);


	pixels_single = (float*)malloc(cnt_photo*realx*realy*sizeof(float));
	if (pixels_single == NULL) {
		fprintf(stderr, "Error: failed to allocate %ldbytes of memory.", cnt_photo*realx*realy*sizeof(float));
		return 1;
	}
	pixeli[0] = 1;
	pixeli[1] = 1;
	mempos = 0;
	fprintf(stderr, "Loading photo files...\n");
	for(int j=0; j<cnt_photo; j++) {
		for(pixeli[1]=realy; pixeli[1]>0; pixeli[1]--) {
			fits_read_pix(photo[j], TFLOAT, pixeli, realx, NULL, pixels_single+realx*pixeli[1]-realx+mempos, NULL, &status);
		}
		mempos += imgsize_mem;
	}
	fprintf(stderr, "Combining photo...\n");
	count = realy;
	kernel = clCreateKernel(program, "photo", &err);
	if (!kernel || err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to create OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	input = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*cnt_photo*realx*realy, NULL, NULL);
	input_bias = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*realx*realy, NULL, NULL);
	input_dark = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*realx*realy, NULL, NULL);
	input_flat = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*realx*realy, NULL, NULL);
	input_exptime_photo = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*cnt_photo, NULL, NULL);
	
	output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*cnt_photo*realx*realy, NULL, NULL);
	if (!input || !output) {
		fprintf(stderr, "Error: Failed to allocate OpenCL device memory.\n");
		return EXIT_FAILURE;
	}    
	err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_photo*realx*realy, pixels_single, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_bias, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_dark, CL_TRUE, 0, sizeof(float)*realx*realy, dark_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_flat, CL_TRUE, 0, sizeof(float)*realx*realy, flat_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_exptime_photo, CL_TRUE, 0, sizeof(float)*cnt_photo, exptime_photo, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to write to OpenCL source array.\n");
		return EXIT_FAILURE;
	}
	imgsize_mem_int = (int)imgsize_mem;
	mid = cnt_photo / 2;
	err = 0;
	err  = clSetKernelArg(kernel, 0, sizeof(cl_mem), &input);
	err |= clSetKernelArg(kernel, 1, sizeof(cl_mem), &output);
	err |= clSetKernelArg(kernel, 2, sizeof(cl_mem), &input_bias);
	err |= clSetKernelArg(kernel, 3, sizeof(cl_mem), &input_dark);
	err |= clSetKernelArg(kernel, 4, sizeof(cl_mem), &input_flat);
	err |= clSetKernelArg(kernel, 5, sizeof(cl_mem), &input_exptime_photo);
	err |= clSetKernelArg(kernel, 6, sizeof(float), &exptime_dark);
	err |= clSetKernelArg(kernel, 7, sizeof(int), &realx);
	err |= clSetKernelArg(kernel, 8, sizeof(int), &realy);
	err |= clSetKernelArg(kernel, 9, sizeof(int), &imgsize_mem_int);
	err |= clSetKernelArg(kernel, 10, sizeof(int), &cnt_photo);
	err |= clSetKernelArg(kernel, 11, sizeof(float), &flat_avg);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to set OpenCL kernel arguments. %d\n", err);
		return EXIT_FAILURE;
	}
	err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
		return EXIT_FAILURE;
	}
	global = (size_t)count;
	err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
	if (err) {
		fprintf(stderr, "Error: Failed to execute OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	clFinish(commands);
	err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy*cnt_photo, photo_comb, 0, NULL, NULL);  
	if (err != CL_SUCCESS) {
		fprintf(stderr, "Error: Failed to read OpenCL output. %d\n", err);
		exit(1);
	}

	free(pixels_single);
	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseMemObject(input_bias);
	clReleaseMemObject(input_dark);
	clReleaseMemObject(input_flat);
	clReleaseMemObject(input_exptime_photo);
	clReleaseProgram(program);
	clReleaseKernel(kernel);

	fprintf(stderr, "Saving outputs...\n");

	int bitpix;
	int naxis;
	long naxes[10];
	int nkeys;
	char buf[1000];
	int hdupos;

	for(int i = 0; i < cnt_photo; i++) {
		char *newname = malloc(strlen(nphoto[i])+10);
		strcpy(newname, "processed-");
		strcat(newname, nphoto[i]);
		unlink(newname);
		fits_create_file(&tmp, newname, &status);

		fits_get_img_param(photo[i], 9, &bitpix, &naxis, naxes, &status);
		fits_create_img(tmp, bitpix, naxis, naxes, &status);
		fits_get_hdu_num(photo[i], &hdupos);
		status=0;
		for(; !status; hdupos++) {
			fits_get_hdrspace(photo[i], &nkeys, NULL, &status);
			for(int j=1; j<=nkeys; j++) {
				fits_read_record(photo[i], j, buf, &status);
				fits_write_record(tmp, buf, &status);
			}
			fits_movrel_hdu(photo[i], 1, NULL, &status);
		}
		if (status == END_OF_FILE) status = 0;

		if (status != 0) {    
			fits_report_error(stderr, status);
			return(status);
		}

		fits_write_img(tmp, TFLOAT, 1, (long long)imgsize_mem, photo_comb+imgsize_mem*i, &status);
		fits_close_file(tmp, &status);
		fprintf(stderr, "Saved the output as %s\n", newname);
		free(newname);
	}

	for(int i = 0; i < cnt_bias; i++) {
		fits_close_file(bias[i], &status);
	}
	for(int i = 0; i < cnt_dark; i++) {
		fits_close_file(dark[i], &status);
	}
	for(int i = 0; i < cnt_flat; i++) {
		fits_close_file(flat[i], &status);
	}
	for(int i = 0; i < cnt_photo; i++) {
		fits_close_file(photo[i], &status);
	}

	free(bias_comb);
	free(dark_comb);
	free(flat_comb);
	free(photo_comb);

	fprintf(stderr, "Processing completed\n");
	#ifdef _WIN32
	#else
	sec = t0.tv_sec, usec = t0.tv_usec;
	gettimeofday(&t0, 0);
	fprintf(stderr, "Took %.3lf seconds.\n", ((double)(t0.tv_sec - sec)*1000000 + (t0.tv_usec - usec))/1000000);
	#endif
}