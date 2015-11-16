#define IMPORT_NOT_BMP 0
#define IMPORT_BIT_WRONG 1
#define IMPORT_SUCCESS 2
#include <stdlib.h>
#include <stdio.h>

struct image {
	char *R, *G, *B;
	int width, height;
};

void newImage(struct image *image, int W, int H) {
	image->width = W - 1;
	image->height = H - 1;
	image->R = (char*)malloc((W + 1) * (H + 1));
	image->G = (char*)malloc((W + 1) * (H + 1));
	image->B = (char*)malloc((W + 1) * (W + 1));
};

void saveImage(struct image image, FILE *file) {
	fputc(0x42, file);
	fputc(0x4D, file);
	fputc((((image.width + 1) * (image.height + 1) * 3) + 54) % ((int)pow(2, 8)), file);
	fputc((((image.width + 1) * (image.height + 1) * 3) + 54) % ((int)pow(2, 16)) / ((int)pow(2,8)), file);
	fputc((((image.width + 1) * (image.height + 1) * 3) + 54) % ((int)pow(2, 24)) / ((int)pow(2,16)), file);
	fputc((((image.width + 1) * (image.height + 1) * 3) + 54) / ((int)pow(2,24)), file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(54, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(40, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc((image.width + 1) % ((int)pow(2, 8)), file);
	fputc((image.width + 1) % ((int)pow(2, 16)) / ((int)pow(2,8)), file);
	fputc((image.width + 1) % ((int)pow(2, 24)) / ((int)pow(2,16)), file);
	fputc((image.width + 1) / ((int)pow(2,24)), file);
	fputc((image.height + 1) % ((int)pow(2, 8)), file);
	fputc((image.height + 1) % ((int)pow(2, 16)) / ((int)pow(2,8)), file);
	fputc((image.height + 1) % ((int)pow(2, 24)) / ((int)pow(2,16)), file);
	fputc((image.height + 1) / ((int)pow(2,24)), file);
	fputc(1, file);
	fputc(0, file);
	fputc(24, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc((((image.width + 1) * (image.height + 1) * 3)) % ((int)pow(2, 8)), file);
	fputc((((image.width + 1) * (image.height + 1) * 3)) % ((int)pow(2, 16)) / ((int)pow(2,8)), file);
	fputc((((image.width + 1) * (image.height + 1) * 3)) % ((int)pow(2, 24)) / ((int)pow(2,16)), file);
	fputc((((image.width + 1) * (image.height + 1) * 3)) / ((int)pow(2,24)), file);
	fputc(0xC4, file);
	fputc(0x0E, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0xC4, file);
	fputc(0x0E, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);
	fputc(0, file);

	char *Rt, *Gt, *Bt;
	Rt = image.R;
	Gt = image.G;
	Bt = image.B;

	if ((image.width + 1) % 4) {
		for(int y = 0; y < (image.height + 1); y++) {
			for(int x = 0; x < (image.width + 1); x++) {
				fputc(*(Bt++), file);
				fputc(*(Gt++), file);
				fputc(*(Rt++), file);
			}
			for(int i = 0; i < (4 - (image.width + 1) * 3 % 4); i++) {
				fputc(255, file);
			}
		}
	} else {
		for(int x = 0; x < ((image.width + 1) * (image.height + 1)); x++) {
			fputc(*(Bt++), file);
			fputc(*(Gt++), file);
			fputc(*(Rt++), file);
		}
	}	
}

void setPixelData(struct image *image, int X, int Y, int data) {
	if (data > 255) data = 255;
	if (data < 0) data = 0;
	*(image->R + X + (Y * (image->width + 1))) = data;
	*(image->G + X + (Y * (image->width + 1))) = data;
	*(image->B + X + (Y * (image->width + 1))) = data;
};

void unloadImage(struct image *image) {
	free(image->R);
	free(image->G);
	free(image->B);
}