#define IMPORT_NOT_BMP 0
#define IMPORT_BIT_WRONG 1
#define IMPORT_SUCCESS 2
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

struct image {
	char *D;
	int width, height;
};

void newImage(struct image *image, int W, int H) {
	image->width = W - 1;
	image->height = H - 1;
	image->D = (char*)malloc((W + 1) * (H + 1));
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

	char *Dt;
	Dt = image.D;

	if ((image.width + 1) % 4) {
		for(int y = 0; y < (image.height + 1); y++) {
			for(int x = 0; x < (image.width + 1); x++) {
				fputc(*(Dt), file);
				fputc(*(Dt), file);
				fputc(*(Dt++), file);
			}
			for(int i = 0; i < (4 - (image.width + 1) * 3 % 4); i++) {
				fputc(255, file);
			}
		}
	} else {
		for(int x = 0; x < ((image.width + 1) * (image.height + 1)); x++) {
			fputc(*(Dt), file);
			fputc(*(Dt), file);
			fputc(*(Dt++), file);
		}
	}	
}

inline void htorgb(float h, char *r, char *g, char *b) {
	if (h < 60) {
		*r = 255;
		*g = h / 60 * 255;
		*b = 0;
	} else if (h < 120) {
		*g = 255;
		*r = 255 - (h - 60) / 60 * 255;
		*b = 0;
	} else if (h < 180) {
		*g = 255;
		*b = (h - 120) / 60 * 255;
		*r = 0;
	} else if (h < 240) {
		*b = 255;
		*g = 255 - (h - 180) / 60 * 255;
		*r = 0;
	} else if (h < 300) {
		*b = 255;
		*r = (h - 240) / 60 * 255;
		*g = 0;
	} else if (h < 360) {
		*r = 255;
		*b = 255 - (h - 300) / 60 * 255;
		*g = 0;
	}
}

void saveImage_rainbow(struct image image, FILE *file) {
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

	unsigned char *Dt;
	float h;
	char rt, gt, bt;
	Dt = (unsigned char*) image.D;

	if ((image.width + 1) % 4) {
		for(int y = 0; y < (image.height + 1); y++) {
			for(int xx = 0; xx < (image.width + 1); xx++) {
				h = 300 - (*(Dt++) / 256.0 * 300);
				htorgb(h, &rt, &gt, &bt);
				fputc(bt, file);
				fputc(gt, file);
				fputc(rt, file);
			}
			for(int i = 0; i < (4 - (image.width + 1) * 3 % 4); i++) {
				fputc(255, file);
			}
		}
	} else {
		for(int xx = 0; xx < ((image.width + 1) * (image.height + 1)); xx++) {
				h = 300 - (*(Dt++) / 256.0 * 300);
				htorgb(h, &rt, &gt, &bt);
				fputc(bt, file);
				fputc(gt, file);
				fputc(rt, file);
		}
	}	
}

void setPixelData(struct image *image, int X, int Y, int data) {
	if (data > 255) data = 255;
	if (data < 0) data = 0;
	*(image->D + X + (Y * (image->width + 1))) = data;
};

void unloadImage(struct image *image) {
	free(image->D);
}