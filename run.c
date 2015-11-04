#ifdef __APPLE__
	#include "/usr/local/Cellar/cfitsio/3.370/include/fitsio.h"
#else
	#include "fitsio.h"
#endif
#include <OpenCL/opencl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define filecnt_max 100

char nbias[filecnt_max][256], ndark[filecnt_max][256], nflat[filecnt_max][256], nphoto[filecnt_max][256];
fitsfile *bias[filecnt_max];
fitsfile *dark[filecnt_max];
fitsfile *flat[filecnt_max];
fitsfile *photo[filecnt_max];

const char *OpenCL_average = " \n\
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
}";

const char *OpenCL_median = " \n\
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
}";

const char *OpenCL_photo = "\n\
__kernel void photo(__global float* input, __global float* output, __global float* input_bias, __global float* input_dark, __global float* input_flat, __global float* exptime_photo, float exptime_dark, int xsize, int ysize, int imgsize, int count, float flat_avg) { \n\
	int num = get_global_id(0)*xsize; \n\
	for(int i = 0; i < xsize; i++) { \n\
		for(int j = 0; j < count; j++) { \n\
			output[j*imgsize+num+i] = ((input[j*imgsize+num+i] - input_bias[num+i]) - (input_dark[num+i] / exptime_dark * exptime_photo[j])) * flat_avg / input_flat[num+i]; \n\
		} \n\
	} \n\
}";

int main(int argc, char *argv[]) {
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

	puts("GPUPhotometry by HLETRD");
	puts("");
	puts("Initializing...");

	err = clGetDeviceIDs(NULL, CL_DEVICE_TYPE_GPU, 1, &device_id, NULL);
	if (err != CL_SUCCESS) {
		puts("Error: Failed to initialize OpenCL device group.");
		return EXIT_FAILURE;
	}

	context = clCreateContext(0, 1, &device_id, NULL, NULL, &err);
	if (!context) {
		puts("Error: Failed to create a OpenCL compute context.");
		return EXIT_FAILURE;
	}

	commands = clCreateCommandQueue(context, device_id, 0, &err);
	if (!commands) {
		puts("Error: Failed to create a OpenCL commands.");
		return EXIT_FAILURE;
	}


	puts("Reading config...");
	if (argc != 2 || access(argv[1], F_OK) != -1) {
		printf("Running by config: %s\n", argv[1]);
		config = fopen(argv[1], "r");
	} else {
		printf("config file not found.\n\nUsage: pm <config_filename>\n");
		return 0;
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


	int status;
	puts("Reading files...");
	for (int i=0; i<cnt_bias; i++) {
		fits_open_file(&bias[i], nbias[i], READONLY, &status);
		if (status) {
			printf("Error reading bias file %s\n", nbias[i]);
			return status;
		}
		fits_get_img_size(bias[i], 2, imgsize, &status);
		printf("Reading bias: %s\n", nbias[i]);
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			printf("Error: size of the bias file %s is different with others.\n", nbias[i]);
			return 1;
		}
	}

	for (int i=0; i<cnt_dark; i++) {
		fits_open_file(&dark[i], ndark[i], READONLY, &status);
		if (status) {
			printf("Error reading dark file %s\n", ndark[i]);
			return status;
		}
		fits_get_img_size(dark[i], 2, imgsize, &status);
		fits_read_key(dark[i], TFLOAT, "EXPTIME", exptime_dark+i, NULL, &status);
		printf("Reading dark: %s (%.3f sec exposure)\n", ndark[i], exptime_dark[i]);
		if (i>0 && exptime_dark[i] != exptime_dark[i-1]) {
			printf("Error: exposure time of dark frames are not equal.\n");
			return 1;
		}
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			printf("Error: size of the dark file %s is different with others.\n", ndark[i]);
			return 1;
		}
	}

	for (int i=0; i<cnt_flat; i++) {
		fits_open_file(&flat[i], nflat[i], READONLY, &status);
		if (status) {
			printf("Error reading flat file %s\n", nflat[i]);
			return status;
		}
		fits_get_img_size(flat[i], 2, imgsize, &status);
		fits_read_key(flat[i], TFLOAT, "EXPTIME", exptime_flat+i, NULL, &status);
		printf("Reading flat: %s (%.3f sec exposure)\n", nflat[i], exptime_flat[i]);
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			printf("Error: size of the flat file %s is different with others.\n", nflat[i]);
			return 1;
		}
	}

	for (int i=0; i<cnt_photo; i++) {
		fits_open_file(&photo[i], nphoto[i], READONLY, &status);
		if (status) {
			printf("Error reading dark file %s\n", nphoto[i]);
			return status;
		}
		fits_get_img_size(photo[i], 2, imgsize, &status);
		fits_read_key(photo[i], TFLOAT, "EXPTIME", exptime_photo+i, NULL, &status);
		printf("Reading photo: %s (%.3f sec exposure)\n", nphoto[i], exptime_photo[i]);
		if (realx == 0 && realy == 0) {
			realx = imgsize[0];
			realy = imgsize[1];
		} else if (realx != imgsize[0] || realy != imgsize[1]) {
			printf("Error: size of the photo file %s is different with others.\n", nphoto[i]);
			return 1;
		}
	}

	float *bias_comb = (float*)calloc(realx*realy, sizeof(float));
	float *dark_comb = (float*)calloc(realx*realy, sizeof(float));
	float *flat_comb = (float*)calloc(realx*realy, sizeof(float));
	float *photo_comb = (float*)calloc(realx*realy*cnt_photo, sizeof(float));
	long imgsize_mem = realx * realy;




	double *pixels = (double*)malloc(cnt_bias*realx*realy*sizeof(double));
	float *pixels_single = (float*)malloc(cnt_bias*realx*realy*sizeof(float));
	if (pixels == NULL) {
		printf("Error: failed to allocate %ldbytes of memory.", cnt_bias*realx*realy*sizeof(double));
		return 1;
	}
	long pixeli[2];
	int count;
	int imgsize_mem_int;
	pixeli[0] = 1;
	pixeli[1] = 1;
	int mempos = 0;
	puts("Loading bias files...");
	for(int j=0; j<cnt_bias; j++){
		for(pixeli[1]=realy; pixeli[1]>0; pixeli[1]--){
			fits_read_pix(bias[j], TDOUBLE, pixeli, realx, NULL, pixels+realx*pixeli[1]-realx+mempos, NULL, &status);
		}
		mempos += imgsize_mem;
	}

	if (cnt_bias > 0) {
		for(int i=0; i<realx*realy*cnt_bias; i++) {
			pixels_single[i] = (float)pixels[i];
		}
		puts("Combining bias...");
		count = realy;
		program = clCreateProgramWithSource(context, 1, (const char **)&OpenCL_average, NULL, &err);
		if (!program) {
			printf("Error: Failed to create OpenCL program.\n");
			return EXIT_FAILURE;
		}
		err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
		if (err != CL_SUCCESS) {
			size_t len;
			char buffer[2048];

			printf("Error: Failed to build program executable!\n");
			clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
			printf("%s\n", buffer);
			return EXIT_FAILURE;
		}
		kernel = clCreateKernel(program, "average", &err);
		if (!kernel || err != CL_SUCCESS) {
			printf("Error: Failed to create OpenCL kernel.\n");
			return EXIT_FAILURE;
		}
		input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_bias*realx*realy, NULL, NULL);
		output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*realx*realy, NULL, NULL);
		if (!input || !output) {
			printf("Error: Failed to allocate OpenCL device memory.\n");
			return EXIT_FAILURE;
		}    
		err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_bias*realx*realy, pixels_single, 0, NULL, NULL);
		if (err != CL_SUCCESS) {
			printf("Error: Failed to write to OpenCL source array.\n");
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
			printf("Error: Failed to set OpenCL kernel arguments. %d\n", err);
			return EXIT_FAILURE;
		}
		err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
		if (err != CL_SUCCESS) {
			printf("Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
			return EXIT_FAILURE;
		}
		global = (size_t)count;
		err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
		if (err) {
			printf("Error: Failed to execute OpenCL kernel.\n");
			return EXIT_FAILURE;
		}
		clFinish(commands);
		err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);  
		if (err != CL_SUCCESS) {
			printf("Error: Failed to read OpenCL output. %d\n", err);
			exit(1);
		}

		clReleaseMemObject(input);
		clReleaseMemObject(output);
		clReleaseProgram(program);
		clReleaseKernel(kernel);
	}


	free(pixels);
	free(pixels_single);
	pixels = (double*)malloc(cnt_dark*realx*realy*sizeof(double));
	pixels_single = (float*)malloc(cnt_dark*realx*realy*sizeof(float));
	if (pixels == NULL) {
		printf("Error: failed to allocate %ldbytes of memory.", cnt_dark*realx*realy*sizeof(double));
		return 1;
	}
	pixeli[0] = 1;
	pixeli[1] = 1;
	mempos = 0;
	puts("Loading dark files...");
	for(int j=0; j<cnt_dark; j++){
		for(pixeli[1]=realy; pixeli[1]>0; pixeli[1]--){
			fits_read_pix(dark[j], TDOUBLE, pixeli, realx, NULL, pixels+realx*pixeli[1]-realx+mempos, NULL, &status);
		}
		mempos += imgsize_mem;
	}
	for(int i=0; i<realx*realy*cnt_dark; i++) {
		pixels_single[i] = (float)pixels[i];
	}
	puts("Combining dark & Subtracting bias...");
	count = realy;
	program = clCreateProgramWithSource(context, 1, (const char **)&OpenCL_average, NULL, &err);
	if (!program) {
		printf("Error: Failed to create OpenCL program.\n");
		return EXIT_FAILURE;
	}
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS) {
		size_t len;
		char buffer[2048];

		printf("Error: Failed to build program executable!\n");
		clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		printf("%s\n", buffer);
		return EXIT_FAILURE;
	}
	kernel = clCreateKernel(program, "average_dark", &err);
	if (!kernel || err != CL_SUCCESS) {
		printf("Error: Failed to create OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_dark*realx*realy, NULL, NULL);
	input_bias = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	if (!input || !output) {
		printf("Error: Failed to allocate OpenCL device memory.\n");
		return EXIT_FAILURE;
	}    
	err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_dark*realx*realy, pixels_single, 0, NULL, NULL);
	err = clEnqueueWriteBuffer(commands, input_bias, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		printf("Error: Failed to write to OpenCL source array.\n");
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
		printf("Error: Failed to set OpenCL kernel arguments. %d\n", err);
		return EXIT_FAILURE;
	}
	err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	if (err != CL_SUCCESS) {
		printf("Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
		return EXIT_FAILURE;
	}
	global = (size_t)count;
	err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
	if (err) {
		printf("Error: Failed to execute OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	clFinish(commands);
	err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy, dark_comb, 0, NULL, NULL);  
	if (err != CL_SUCCESS) {
		printf("Error: Failed to read OpenCL output. %d\n", err);
		exit(1);
	}

	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseMemObject(input_bias);
	clReleaseProgram(program);
	clReleaseKernel(kernel);



	free(pixels);
	free(pixels_single);
	pixels = (double*)malloc(cnt_flat*realx*realy*sizeof(double));
	pixels_single = (float*)malloc(cnt_flat*realx*realy*sizeof(float));
	if (pixels == NULL) {
		printf("Error: failed to allocate %ldbytes of memory.", cnt_flat*realx*realy*sizeof(double));
		return 1;
	}
	pixeli[0] = 1;
	pixeli[1] = 1;
	mempos = 0;
	puts("Loading flat files...");
	for(int j=0; j<cnt_flat; j++) {
		for(pixeli[1]=realy; pixeli[1]>0; pixeli[1]--){
			fits_read_pix(flat[j], TDOUBLE, pixeli, realx, NULL, pixels+realx*pixeli[1]-realx+mempos, NULL, &status);
		}
		mempos += imgsize_mem;
	}
	for(int i=0; i<realx*realy*cnt_flat; i++) {
		pixels_single[i] = (float)pixels[i];
	}
	count = realy;
	puts("Combining flat & Subtracting bias, dark...");
	program = clCreateProgramWithSource(context, 1, (const char **)&OpenCL_median, NULL, &err);
	if (!program) {
		printf("Error: Failed to create OpenCL program.\n");
		return EXIT_FAILURE;
	}
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS) {
		size_t len;
		char buffer[2048];

		printf("Error: Failed to build program executable!\n");
		clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		printf("%s\n", buffer);
		return EXIT_FAILURE;
	}
	kernel = clCreateKernel(program, "median_flat", &err);
	if (!kernel || err != CL_SUCCESS) {
		printf("Error: Failed to create OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	input = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_flat*realx*realy, NULL, NULL);
	input_dark = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	input_exptime_flat = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*cnt_flat, NULL, NULL);
	input_bias = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*realx*realy, NULL, NULL);
	if (!input || !input_dark || !output) {
		printf("Error: Failed to allocate OpenCL device memory.\n");
		return EXIT_FAILURE;
	}    
	err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_flat*realx*realy, pixels_single, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_dark, CL_TRUE, 0, sizeof(float)*realx*realy, dark_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_exptime_flat, CL_TRUE, 0, sizeof(float)*cnt_flat, exptime_flat, 0, NULL, NULL);
	err = clEnqueueWriteBuffer(commands, input_bias, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		printf("Error: Failed to write to OpenCL source array.\n");
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
		printf("Error: Failed to set OpenCL kernel arguments. %d\n", err);
		return EXIT_FAILURE;
	}
	err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	if (err != CL_SUCCESS) {
		printf("Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
		return EXIT_FAILURE;
	}
	global = (size_t)count;
	err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
	if (err) {
		printf("Error: Failed to execute OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	clFinish(commands);
	err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy, flat_comb, 0, NULL, NULL);  
	if (err != CL_SUCCESS) {
		printf("Error: Failed to read OpenCL output. %d\n", err);
		exit(1);
	}

	double flatsum = 0;
	for(int i=0; i<imgsize_mem_int; i++) {
		flatsum += (double)flat_comb[i];
	}
	float flat_avg = flatsum / imgsize_mem_int;
	free(pixels);
	free(pixels_single);
	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseMemObject(input_dark);
	clReleaseMemObject(input_exptime_flat);
	clReleaseMemObject(input_bias);
	clReleaseProgram(program);
	clReleaseKernel(kernel);

	pixels = (double*)malloc(cnt_photo*realx*realy*sizeof(double));
	pixels_single = (float*)malloc(cnt_photo*realx*realy*sizeof(float));
	if (pixels == NULL) {
		printf("Error: failed to allocate %ldbytes of memory.", cnt_photo*realx*realy*sizeof(double));
		return 1;
	}
	pixeli[0] = 1;
	pixeli[1] = 1;
	mempos = 0;
	puts("Loading photo files...");
	for(int j=0; j<cnt_photo; j++) {
		for(pixeli[1]=realy; pixeli[1]>0; pixeli[1]--){
			fits_read_pix(photo[j], TDOUBLE, pixeli, realx, NULL, pixels+realx*pixeli[1]-realx+mempos, NULL, &status);
		}
		mempos += imgsize_mem;
	}
	puts("Combining photo...");

	for(int i=0; i<realx*realy*cnt_photo; i++) {
		pixels_single[i] = (float)pixels[i];
	}
	count = realy;
	program = clCreateProgramWithSource(context, 1, (const char **)&OpenCL_photo, NULL, &err);
	if (!program) {
		printf("Error: Failed to create OpenCL program.\n");
		return EXIT_FAILURE;
	}
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
	if (err != CL_SUCCESS) {
		size_t len;
		char buffer[2048];

		printf("Error: Failed to build program executable!\n");
		clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, sizeof(buffer), buffer, &len);
		printf("%s\n", buffer);
		return EXIT_FAILURE;
	}
	kernel = clCreateKernel(program, "photo", &err);
	if (!kernel || err != CL_SUCCESS) {
		printf("Error: Failed to create OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	input = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*cnt_photo*realx*realy, NULL, NULL);
	input_bias = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*realx*realy, NULL, NULL);
	input_dark = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*realx*realy, NULL, NULL);
	input_flat = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*realx*realy, NULL, NULL);
	input_exptime_photo = clCreateBuffer(context, CL_MEM_READ_ONLY,  sizeof(float)*cnt_photo, NULL, NULL);
	
	output = clCreateBuffer(context, CL_MEM_WRITE_ONLY, sizeof(float)*cnt_photo*realx*realy, NULL, NULL);
	if (!input || !output) {
		printf("Error: Failed to allocate OpenCL device memory.\n");
		return EXIT_FAILURE;
	}    
	err = clEnqueueWriteBuffer(commands, input, CL_TRUE, 0, sizeof(float)*cnt_photo*realx*realy, pixels_single, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_bias, CL_TRUE, 0, sizeof(float)*realx*realy, bias_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_dark, CL_TRUE, 0, sizeof(float)*realx*realy, dark_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_flat, CL_TRUE, 0, sizeof(float)*realx*realy, flat_comb, 0, NULL, NULL);
	err |= clEnqueueWriteBuffer(commands, input_exptime_photo, CL_TRUE, 0, sizeof(float)*cnt_photo, exptime_photo, 0, NULL, NULL);
	if (err != CL_SUCCESS) {
		printf("Error: Failed to write to OpenCL source array.\n");
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
		printf("Error: Failed to set OpenCL kernel arguments. %d\n", err);
		return EXIT_FAILURE;
	}
	err = clGetKernelWorkGroupInfo(kernel, device_id, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, NULL);
	if (err != CL_SUCCESS) {
		printf("Error: Failed to retrieve OpenCL kernel work group info. %d\n", err);
		return EXIT_FAILURE;
	}
	global = (size_t)count;
	err = clEnqueueNDRangeKernel(commands, kernel, 1, NULL, &global, &local, 0, NULL, NULL);
	if (err) {
		printf("Error: Failed to execute OpenCL kernel.\n");
		return EXIT_FAILURE;
	}
	clFinish(commands);
	err = clEnqueueReadBuffer(commands, output, CL_TRUE, 0, sizeof(float)*realx*realy*cnt_photo, photo_comb, 0, NULL, NULL);  
	if (err != CL_SUCCESS) {
		printf("Error: Failed to read OpenCL output. %d\n", err);
		exit(1);
	}

	free(pixels);
	free(pixels_single);
	clReleaseMemObject(input);
	clReleaseMemObject(output);
	clReleaseMemObject(input_bias);
	clReleaseMemObject(input_dark);
	clReleaseMemObject(input_flat);
	clReleaseMemObject(input_exptime_photo);
	clReleaseProgram(program);
	clReleaseKernel(kernel);

	puts("Saving outputs...");

	fitsfile *tmp;

	int bitpix;
	int naxis;
	long naxes[10];
	int nkeys;
	char buf[100];
	int hdupos;


	free(bias_comb);
	free(dark_comb);
	free(flat_comb);

	for(int i = 0; i < cnt_photo; i++) {
		char *newname = malloc(strlen(nphoto[i])+10);
		strcpy(newname, "processed-");
		strcat(newname, nphoto[i]);
		unlink(newname);
		fits_create_file(&tmp, newname, &status);

		fits_get_img_param(bias[0], 9, &bitpix, &naxis, naxes, &status);
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
		printf("Saved the output as %s\n", newname);
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

	free(photo_comb);

	puts("Processing completed");
}