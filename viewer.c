#include <gtk/gtk.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include "image.h"
#ifdef __APPLE__
	#include "/usr/local/Cellar/cfitsio/3.370/include/fitsio.h"
#else
	#include "fitsio.h"
#endif
#ifndef M_PI
    #define M_PI 3.141592653589793238462643383279
#endif
#if GTK_MINOR_VERSION > 11
	#define gtk_widget_set_margin_start_my(a, b) gtk_widget_set_margin_start(a,b)
	#define gtk_widget_set_margin_end_my(a, b) gtk_widget_set_margin_end(a,b)
#else
	#define gtk_widget_set_margin_start_my(a, b) gtk_widget_set_margin_left(a,b)
	#define gtk_widget_set_margin_end_my(a, b) gtk_widget_set_margin_right(a,b)
#endif
fitsfile *file;
GtkWidget *window, *window_sub, *window_zoom, *window_options, *window_header, *window_profile;
GtkWidget *canvas, *eventbox;
GtkWidget *container;
GtkWidget *button_box1, *button1, *button_box2, *button2;
GtkWidget *label1, *label2, *label_status, *label_diff;
GtkWidget *label_adu, *label_adu_max, *label_sky, *button_sky, *buttonbox_sky, *button_reset, *buttonbox_reset, *label_stat, *label_mag, *label_mag_max;
GtkWidget *scroll, *canvas_zoom;
GdkPixbuf *pixbuf, *pixbuf_tmp, *pixbuf_full;
GtkWidget *combo, *combo_prev, *combo_ap;
GtkWidget *label_zoom1, *label_zoom2, *label_ap;
GtkListStore *liststore;
GtkCellRenderer *column;
GtkWidget *histoprev;
GtkWidget *scalemin, *scalemax;
GtkWidget *button_refresh, *buttonbox_refresh, *button_autoscale, *buttonbox_autoscale;
GtkWidget *text_header;
GtkWidget *eventbox_header;
GtkWidget *draw_profiles;
GtkWidget *label_color, *combo_color;
GtkTextBuffer *textbuf_header;
int zoom_lastx, zoom_lasty;
cairo_surface_t *surface;
cairo_t *cr;
int imgx, imgy;
int imgsize_mem;
int min, max;
float mag = 0.375;
float prev_zoom = 2.0;
int *pixels;
int mode = 0;
long long int sum_sky = 0;
int cnt_sky = 0;
double avg = 0, stdev = 0;
double avg_o, stdev_o;
double mag1, mag2;
int viewerx, viewery;
int histogram[512];
int scalemin_set, scalemax_set;
char headers[65536];
int profiles[256];

int apsize = 5;

int adu;

static void click1() {
	gtk_label_set_text(GTK_LABEL(label_status), "Selecting reference star");
	mode = 1;
}

static void click2() {
	gtk_label_set_text(GTK_LABEL(label_status), "Selecting target star");
	mode = 2;
}

gboolean button_press_callback(GtkWidget *eventbox, GdkEventButton *event, gpointer data) {
	int x = event->x, y = event->y;
	int pixcnt = 0;
	gint wx, wy;
	gtk_window_get_size(GTK_WINDOW(window), &wx, &wy);
	if (viewerx == wx || viewery == wx) {
		x -= (wx - imgx * mag) / 2;
		y -= (wy - imgy * mag) / 2;
	}
	if (x < 0 || y < 0 || x >= imgx * mag || y >= imgy * mag) return 0;
	int pixval = 0;
	for (int i = -apsize; i <= apsize; i++) {
		for (int j = -((int)sqrt(apsize * apsize - i * i)); j*j + i*i <= apsize*apsize; j++) {
			pixval += pixels[(int)((x+i)/mag + (y+j)*imgx/mag)];
			pixcnt++;
		}
	}
	pixval /= pixcnt;
	char s[100];
	if (mode == 1) {
		if (cnt_sky) {
			sprintf(s, "Reference: %lf", -2.5 * log10(pixval - (double)sum_sky / cnt_sky));
			mag1 = -2.5 * log10(pixval - (double)sum_sky / cnt_sky);
		} else {
			sprintf(s, "Reference: %lf", -2.5 * log10(pixval));
			mag1 = -2.5 * log10(pixval);
		}
		gtk_label_set_text(GTK_LABEL(label1), s);
		sprintf(s, "Difference: %lf", mag2 - mag1);
		gtk_label_set_text(GTK_LABEL(label_diff), s);
	} else if (mode == 2) {
		if (cnt_sky) {
			sprintf(s, "Target: %lf", -2.5 * log10(pixval - (double)sum_sky / cnt_sky));
			mag2 = -2.5 * log10(pixval - (double)sum_sky / cnt_sky);
		} else {
			sprintf(s, "Target: %lf", -2.5 * log10(pixval));
			mag2 = -2.5 * log10(pixval);
		}
		gtk_label_set_text(GTK_LABEL(label2), s);
		sprintf(s, "Difference: %lf", mag2 - mag1);
		gtk_label_set_text(GTK_LABEL(label_diff), s);
	} else if (mode == 3) {
		sum_sky += pixval;
		cnt_sky++;
		sprintf(s, "Sky value: %lf", (double)sum_sky / cnt_sky);
		gtk_label_set_text(GTK_LABEL(label_sky), s);
	}
	mode = 0;
	gtk_label_set_text(GTK_LABEL(label_status), "Select mode");
	return 0;
}

gboolean mousemove_callback(GtkWidget *eventbox, GdkEventButton *event, gpointer data) {
	int x = event->x, y = event->y;
	int pixcnt = 0;
	gint wx, wy;
	gtk_window_get_size(GTK_WINDOW(window), &wx, &wy);
	if (viewerx == wx || viewery == wy) {
		x -= (wx - imgx * mag) / 2;
		y -= (wy - imgy * mag) / 2;
	}
	if (x < 0 || y < 0 || x > imgx * mag || y > imgy * mag) return 0;
	if (x >= apsize*mag && y >= apsize*mag && x <= imgx*mag - apsize*mag && y <= imgy*mag - apsize*mag) {
		int pixval = 0;
		for (int i = -apsize; i <= apsize; i++) {
			for (int j = -((int)sqrt(apsize * apsize - i * i)); j*j + i*i <= apsize*apsize; j++) {
				if ((int)((x+i)/mag + (y+j)*imgx/mag) >= 0 && (int)((x+i)/mag + (y+j)*imgx/mag) < imgx*imgy) {
					pixval += pixels[(int)((x+i)/mag + (y+j)*imgx/mag)];
					pixcnt++;
				}
			}
		}
		pixval /= pixcnt;
		char s[100];
		sprintf(s, "ADU(point): %d", pixval);
		gtk_label_set_text(GTK_LABEL(label_adu), s);
		if (cnt_sky) {
			sprintf(s, "Mag(point): %lf", -2.5 * log10(pixval - (double)sum_sky / cnt_sky));
		} else {
			sprintf(s, "Mag(point): %lf", -2.5 * log10(pixval));
		}
		gtk_label_set_text(GTK_LABEL(label_mag), s);

		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, imgx, imgy);
		cr = cairo_create(surface);
		gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
		cairo_set_line_width(cr, 1.0);
		cairo_paint(cr);
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_arc(cr, x, y, apsize*mag, 0, 2*M_PI);
		cairo_stroke(cr);
		pixbuf_tmp = gdk_pixbuf_get_from_surface(surface, 0, 0, imgx*mag, imgy*mag);
		gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf_tmp);
		cairo_surface_destroy(surface);
		cairo_destroy(cr);
		g_object_unref(pixbuf_tmp);

		gtk_window_get_size(GTK_WINDOW(window_zoom), &wx, &wy);
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, wx, wy);
		cr = cairo_create(surface);
		cairo_scale(cr, prev_zoom, prev_zoom);
		gdk_cairo_set_source_pixbuf(cr, pixbuf_full, (double)-x/mag+wx/2/prev_zoom, (double)-y/mag+wy/2/prev_zoom);
		cairo_paint(cr);
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_set_line_width(cr, 1.0);
		cairo_arc(cr, wx/2/prev_zoom, wy/2/prev_zoom, apsize, 0, 2*M_PI);
		cairo_stroke(cr);
		pixbuf_tmp = gdk_pixbuf_get_from_surface(surface, 0, 0, wx, wy);
		gtk_image_set_from_pixbuf(GTK_IMAGE(canvas_zoom), pixbuf_tmp);
		cairo_surface_destroy(surface);
		cairo_destroy(cr);
		g_object_unref(pixbuf_tmp);
		zoom_lastx = x;
		zoom_lasty = y;

		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 512, 150);
		cr = cairo_create(surface);
		int pos;
		float max;
		float peak = 0.1;
		int peakx, peaky;
		for (int i = -apsize; i <= apsize; i++) {
			for (int j = -((int)sqrt(apsize * apsize - i * i)); j*j + i*i <= apsize*apsize; j++) {
				if ((int)((x+i)/mag + (y+j)*imgx/mag) >= 0 && (int)((x+i)/mag + (y+j)*imgx/mag) < imgx*imgy) {
					if (pixels[(int)((x+i)/mag + (y+j)*imgx/mag)] > peak) {
						peak = pixels[(int)((x+i)/mag + (y+j)*imgx/mag)];
						peakx = x+i;
						peaky = y+j;
					}
				}
			}
		}
		//x = peakx;
		//y = peaky;

		sprintf(s, "ADU(max): %d", (int)peak);
		gtk_label_set_text(GTK_LABEL(label_adu_max), s);
		if (cnt_sky) {
			sprintf(s, "Magnitude(max): %lf", -2.5 * log10(peak - (double)sum_sky / cnt_sky));
		} else {
			sprintf(s, "Magnitude(max): %lf", -2.5 * log10(peak));
		}
		gtk_label_set_text(GTK_LABEL(label_mag_max), s);

		cairo_set_source_rgb(cr, 0, 0, 0);
		cairo_rectangle(cr, 0, 0, 512, 150);
		cairo_paint(cr);
		cairo_set_line_width(cr, 1.0);
		cairo_set_source_rgb(cr, 1, 0.3, 0.3);
		cairo_move_to(cr, 0, 150);
		max = 0.1;
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag + i);
			if (pos >= 0 && pos < imgsize_mem) {
				if (pixels[pos] > max) max = pixels[pos];
			}
		}
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag + i);
			if (pos >= 0 && pos < imgsize_mem) {
				cairo_line_to(cr, (i+32)*8, 150-pixels[pos]/max*140);
			} else {
				cairo_line_to(cr, (i+32)*8, 150);
			}
		}
		cairo_line_to(cr, 512, 150);
		cairo_stroke(cr);
		cairo_set_source_rgb(cr, 0.3, 1, 0.3);
		cairo_move_to(cr, 0, 150);
		max = 0.1;
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag + i*imgx);
			if (pos >= 0 && pos < imgsize_mem) {
				if (pixels[pos] > max) max = pixels[pos];
			}
		}
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag + i*imgx);
			if (pos >= 0 && pos < imgsize_mem) {
				cairo_line_to(cr, (i+32)*8, 150-pixels[pos]/max*140);
			} else {
				cairo_line_to(cr, (i+32)*8, 150);
			}
		}
		cairo_line_to(cr, 512, 150);
		cairo_stroke(cr);
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_move_to(cr, 0, 150);
		max = 0.1;
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag + i*imgx + i);
			if (pos >= 0 && pos < imgsize_mem) {
				if (pixels[pos] > max) max = pixels[pos];
			}
		}
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag + i*imgx + i);
			if (pos >= 0 && pos < imgsize_mem) {
				cairo_line_to(cr, (i+32)*8, 150-pixels[pos]/max*140);
			} else {
				cairo_line_to(cr, (i+32)*8, 150);
			}
		}
		cairo_line_to(cr, 512, 150);
		cairo_stroke(cr);
		cairo_set_source_rgb(cr, 0, 1, 1);
		cairo_move_to(cr, 0, 150);
		max = 0.1;
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag - i*imgx + i);
			if (pos >= 0 && pos < imgsize_mem) {
				if (pixels[pos] > max) max = pixels[pos];
			}
		}
		for(int i = -32; i <= 32; i++) {
			pos = (int)(y/mag*imgx + x/mag - i*imgx + i);
			if (pos >= 0 && pos < imgsize_mem) {
				cairo_line_to(cr, (i+32)*8, 150-pixels[pos]/max*140);
			} else {
				cairo_line_to(cr, (i+32)*8, 150);
			}
		}
		cairo_line_to(cr, 512, 150);
		cairo_stroke(cr);
		pixbuf_tmp = gdk_pixbuf_get_from_surface(surface, 0, 0, 512, 150);
		gtk_image_set_from_pixbuf(GTK_IMAGE(draw_profiles), pixbuf_tmp);
		cairo_surface_destroy(surface);
		cairo_destroy(cr);
		g_object_unref(pixbuf_tmp);
	}
	return 0;
}

void makeimg() {
	struct image bmp;
	int pixelval;
	newImage(&bmp, (int)(imgx * mag), (int)(imgy * mag));
	if (mag == 1.0) {
		for (int i = 0; i < imgy; i++) {
			for (int j = 0; j < imgx; j++) {
				pixelval = ((pixels[i*imgx+j] - avg) / stdev * 256);
				setPixelData(&bmp, j, (int)imgy-i, pixelval);		
			}
		}
	} else {
		for (int i = 0; i < imgy * mag; i++) {
			for (int j = 0; j < imgx * mag; j++) {
				pixelval = (double)((pixels[((int)(i/mag)*imgx)+(int)(j/mag)] - avg) / stdev * 256);
				setPixelData(&bmp, j, (int)imgy*mag-i, pixelval);		
			}
		}
	}
	unlink("tmp.bmp");
	FILE *tmp = fopen("tmp.bmp", "w");
	saveImage(bmp, tmp);
	unloadImage(&bmp);
	fclose(tmp);
}

void makeimg_rainbow() {
	struct image bmp;
	int pixelval;
	newImage(&bmp, (int)(imgx * mag), (int)(imgy * mag));
	if (mag == 1.0) {
		for (int i = 0; i < imgy; i++) {
			for (int j = 0; j < imgx; j++) {
				pixelval = ((pixels[i*imgx+j] - avg) / stdev * 256);
				setPixelData(&bmp, j, (int)imgy-i, pixelval);		
			}
		}
	} else {
		for (int i = 0; i < imgy * mag; i++) {
			for (int j = 0; j < imgx * mag; j++) {
				pixelval = (double)((pixels[((int)(i/mag)*imgx)+(int)(j/mag)] - avg) / stdev * 256);
				setPixelData(&bmp, j, (int)imgy*mag-i, pixelval);		
			}
		}
	}
	unlink("tmp.bmp");
	FILE *tmp = fopen("tmp.bmp", "w");
	saveImage_rainbow(bmp, tmp);
	unloadImage(&bmp);
	fclose(tmp);
}

void makeimg_preview() {
	struct image bmp;
	int pixelval;
	newImage(&bmp, (int)(imgx), (int)(imgy));
	for (int i = 0; i < imgy; i++) {
		for (int j = 0; j < imgx; j++) {
			pixelval = (double)((pixels[(int)(i*imgx+j)] - avg) / stdev * 256);
			setPixelData(&bmp, j, (int)imgy-i, pixelval);		
		}
	}
	unlink("tmp_full.bmp");
	FILE *tmp = fopen("tmp_full.bmp", "w");
	saveImage(bmp, tmp);
	unloadImage(&bmp);
	fclose(tmp);
}

void makeimg_preview_rainbow() {
	struct image bmp;
	int pixelval;
	newImage(&bmp, (int)(imgx), (int)(imgy));
	for (int i = 0; i < imgy; i++) {
		for (int j = 0; j < imgx; j++) {
			pixelval = (double)((pixels[(int)(i*imgx+j)] - avg) / stdev * 256);
			setPixelData(&bmp, j, (int)imgy-i, pixelval);		
		}
	}
	unlink("tmp_full.bmp");
	FILE *tmp = fopen("tmp_full.bmp", "w");
	saveImage_rainbow(bmp, tmp);
	unloadImage(&bmp);
	fclose(tmp);
}

void resize_callback(int always) {
	static gint wx, wy, wx_new, wy_new;
	static float prev_zoom_tmp;
	gtk_window_get_size(GTK_WINDOW(window_zoom), &wx_new, &wy_new);
	if (wx == 0 && wy == 0) {
		wx = wx_new;
		wy = wy_new;
	}
	if (wx_new != wx || wy_new != wy || prev_zoom_tmp != prev_zoom || always) {
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, wx, wy);
		cr = cairo_create(surface);
		cairo_scale(cr, prev_zoom, prev_zoom);
		gdk_cairo_set_source_pixbuf(cr, pixbuf_full, (double)-zoom_lastx/mag+wx/2/prev_zoom, (double)-zoom_lasty/mag+wy/2/prev_zoom);
		cairo_paint(cr);
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_set_line_width(cr, 1.0);
		cairo_arc(cr, wx/2/prev_zoom, wy/2/prev_zoom, apsize, 0, 2*M_PI);
		cairo_stroke(cr);
		pixbuf_tmp = gdk_pixbuf_get_from_surface(surface, 0, 0, wx, wy);
		gtk_image_set_from_pixbuf(GTK_IMAGE(canvas_zoom), pixbuf_tmp);
		cairo_surface_destroy(surface);
		cairo_destroy(cr);
		g_object_unref(pixbuf_tmp);
		wx = wx_new;
		wy = wy_new;
		prev_zoom_tmp = prev_zoom;
	}
}


gboolean sky_pick() {
	gtk_label_set_text(GTK_LABEL(label_status), "Selecting sky");
	mode = 3;
	return 0;
}

gboolean sky_reset() {
	sum_sky = 0;
	cnt_sky = 0;
	gtk_label_set_text(GTK_LABEL(label_sky), "Sky value: 0.000000");
	return 0;
}

void getsize(GtkWidget *widget, GtkAllocation *allocation) {
	viewerx = allocation->width;
	viewery = allocation->height;
}

void destroy() {
	exit(0);
}

void combosel() {
	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo))) {
		case 0:
		mag = 0.125;
		break;
		case 1:
		mag = 0.25;
		break;
		case 2:
		mag = 0.375;
		break;
		case 3:
		mag = 0.5;
		break;
		case 4:
		mag = 0.625;
		break;
		case 5:
		mag = 0.75;
		break;
		case 6:
		mag = 0.875;
		break;
		case 7:
		mag = 1.0;
		break;
	}
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 0) {
		makeimg();
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 1) {
		makeimg_rainbow();
	}
	g_object_unref(pixbuf);
	GError **error = NULL;
	pixbuf = gdk_pixbuf_new_from_file("tmp.bmp", error);
	unlink("tmp.bmp");
	gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf);
}

void combosel_prev() {
	GError **error = NULL;
	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_prev))) {
		case 0:
		prev_zoom = 0.5;
		break;
		case 1:
		prev_zoom = 1.0;
		break;
		case 2:
		prev_zoom = 1.5;
		break;
		case 3:
		prev_zoom = 2.0;
		break;
		case 4:
		prev_zoom = 2.5;
		break;
		case 5:
		prev_zoom = 3.0;
		break;
		case 6:
		prev_zoom = 3.5;
		break;
		case 7:
		prev_zoom = 4.0;
		break;
		case 8:
		prev_zoom = 4.5;
		break;
		case 9:
		prev_zoom = 5.0;
		break;
		case 10:
		prev_zoom = 5.5;
		break;
		case 11:
		prev_zoom = 6.0;
		break;
		case 12:
		prev_zoom = 6.5;
		break;
		case 13:
		prev_zoom = 7.0;
		break;
		case 14:
		prev_zoom = 7.5;
		break;
		case 15:
		prev_zoom = 8.0;
		break;
	}
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 0) {
		makeimg_preview();
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 1) {
		makeimg_preview_rainbow();
	}
	g_object_unref(pixbuf_full);
	pixbuf_full = gdk_pixbuf_new_from_file("tmp_full.bmp", error);
	unlink("tmp_full.bmp");
	resize_callback(TRUE);
}

void combosel_ap() {
	apsize = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_ap)) + 1;
	resize_callback(TRUE);
}

void combosel_color() {
	GError **error = NULL;
	switch (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color))) {
		case 0:
		makeimg();
		g_object_unref(pixbuf);
		pixbuf = gdk_pixbuf_new_from_file("tmp.bmp", error);
		unlink("tmp.bmp");
		gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf);
		makeimg_preview();
		g_object_unref(pixbuf_full);
		pixbuf_full = gdk_pixbuf_new_from_file("tmp_full.bmp", error);
		unlink("tmp_full.bmp");
		resize_callback(TRUE);
		break;
		case 1:
		makeimg_rainbow();
		g_object_unref(pixbuf);
		pixbuf = gdk_pixbuf_new_from_file("tmp.bmp", error);
		unlink("tmp.bmp");
		gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf);
		makeimg_preview_rainbow();
		g_object_unref(pixbuf_full);
		pixbuf_full = gdk_pixbuf_new_from_file("tmp_full.bmp", error);
		unlink("tmp_full.bmp");
		resize_callback(TRUE);
		break;
	}
}

void draw_histogram(GtkWidget *widget, cairo_t *cr, gpointer data) {
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, 0, 0, 512, 150);
	cairo_fill(cr);
	cairo_set_line_width(cr, 1.0);
	float histogram_max = 0;
	float histogram_disp[512];
	for (int i = 0; i < 512; i++) {
		histogram_disp[i] = histogram[i];
		if (histogram_max < histogram_disp[i]) histogram_max = histogram_disp[i];
	}
	//if (histogram_max > imgsize_mem/4096) histogram_max = imgsize_mem/4096;
	cairo_set_source_rgb(cr, 1, 1, 1);
	for (int i = 0; i < 512; i++) {
		cairo_move_to(cr, i, 150-(int)(histogram_disp[i]/histogram_max*150));
		cairo_line_to(cr, i, 150);
		cairo_stroke(cr);
	}
}

void draw_profiles_func(GtkWidget *widget, cairo_t *cr, gpointer data) {
	cairo_set_source_rgb(cr, 0, 0, 0);
	cairo_rectangle(cr, 0, 0, 512, 150);
	cairo_fill(cr);
}

void scale() {
	scalemin_set = gtk_range_get_value(GTK_RANGE(scalemin));
	scalemax_set = gtk_range_get_value(GTK_RANGE(scalemax));
	avg = scalemin_set;
	stdev = scalemax_set - scalemin_set;
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 0) {
		makeimg();
		makeimg_preview();
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 1) {
		makeimg_rainbow();
		makeimg_preview_rainbow();
	}
	g_object_unref(pixbuf);
	GError **error = NULL;
	pixbuf = gdk_pixbuf_new_from_file("tmp.bmp", error);
	unlink("tmp.bmp");
	gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf);
	g_object_unref(pixbuf_full);
	pixbuf_full = gdk_pixbuf_new_from_file("tmp_full.bmp", error);
	unlink("tmp_full.bmp");
	resize_callback(TRUE);
}

void scale_auto() {
	avg = avg_o;
	stdev = stdev_o;
	gtk_range_set_value(GTK_RANGE(scalemin), avg);
	gtk_range_set_value(GTK_RANGE(scalemax), avg+stdev);
	if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 0) {
		makeimg();
		makeimg_preview();
	} else if (gtk_combo_box_get_active(GTK_COMBO_BOX(combo_color)) == 1) {
		makeimg_rainbow();
		makeimg_preview_rainbow();
	}
	g_object_unref(pixbuf);
	GError **error = NULL;
	pixbuf = gdk_pixbuf_new_from_file("tmp.bmp", error);
	unlink("tmp.bmp");
	gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf);
	g_object_unref(pixbuf_full);
	pixbuf_full = gdk_pixbuf_new_from_file("tmp_full.bmp", error);
	unlink("tmp_full.bmp");
	resize_callback(TRUE);
}

void activate(GtkApplication* app, gpointer user_data) {
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "FITS Viewer");
	gtk_window_set_default_size(GTK_WINDOW(window), 768, 768);
	g_signal_connect(window, "destroy", G_CALLBACK(destroy), NULL);

	canvas = gtk_image_new();
	GError **error = NULL;
	pixbuf = gdk_pixbuf_new_from_file("tmp.bmp", error);
	unlink("tmp.bmp");
	gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf);

	eventbox = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(eventbox), canvas);
	g_signal_connect(G_OBJECT(eventbox), "button_press_event", G_CALLBACK(button_press_callback), canvas);
	g_signal_connect(eventbox, "size-allocate", G_CALLBACK(getsize), NULL);

	gtk_widget_add_events(eventbox, GDK_POINTER_MOTION_MASK);
	g_signal_connect(G_OBJECT(eventbox), "motion-notify-event", G_CALLBACK(mousemove_callback), canvas);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_ALWAYS, GTK_POLICY_ALWAYS);
	gtk_widget_set_size_request(scroll, 768, 768);
	gtk_container_add(GTK_CONTAINER(scroll), eventbox);

	gtk_container_add(GTK_CONTAINER(window), scroll);
	gtk_widget_show_all(window);


	window_zoom = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window_zoom), "Preview");
	gtk_window_set_default_size(GTK_WINDOW(window_zoom), 512, 512);
	pixbuf_full = gdk_pixbuf_new_from_file("tmp_full.bmp", error);
	unlink("tmp_full.bmp");
	canvas_zoom = gtk_image_new();
	eventbox = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(eventbox), canvas_zoom);
	g_signal_connect(G_OBJECT(eventbox), "size_allocate", G_CALLBACK(resize_callback), NULL);
	gtk_container_add(GTK_CONTAINER(window_zoom), eventbox);
	gtk_widget_show_all(window_zoom);



	window_sub = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window_sub), "Controls");
	gtk_window_set_resizable(GTK_WINDOW(window_sub), FALSE);

	container = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(window_sub), container);
	gtk_widget_set_margin_start_my(container, 20);
	gtk_widget_set_margin_end_my(container, 30);

	button_box1 = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(button_box1, 10);
	gtk_grid_attach(GTK_GRID(container), button_box1, 0, 0, 2, 1);
	button1 = gtk_button_new_with_label("Pick reference star");
	g_signal_connect(button1, "clicked", G_CALLBACK(click1), NULL);
	gtk_container_add(GTK_CONTAINER(button_box1), button1);
	
	button_box2 = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(button_box2, 10);
	gtk_grid_attach(GTK_GRID(container), button_box2, 2, 0, 2, 1);
	button2 = gtk_button_new_with_label("Pick target star");
	g_signal_connect(button2, "clicked", G_CALLBACK(click2), NULL);
	gtk_container_add(GTK_CONTAINER(button_box2), button2);


	label1 = gtk_label_new("Reference: 0.000000");
	gtk_widget_set_margin_top(label1, 10);
	gtk_grid_attach(GTK_GRID(container), label1, 0, 1, 1, 1);

	label2 = gtk_label_new("Target: 0.000000");
	gtk_widget_set_margin_top(label2, 10);
	gtk_grid_attach(GTK_GRID(container), label2, 1, 1, 1, 1);

	label_diff = gtk_label_new("Difference: 0.000000");
	gtk_widget_set_margin_top(label_diff, 10);
	gtk_grid_attach(GTK_GRID(container), label_diff, 2, 1, 1, 1);


	label_adu = gtk_label_new("ADU(point): 0");
	gtk_widget_set_margin_start_my(label_adu, 20);
	gtk_widget_set_margin_top(label_adu, 15);
	gtk_grid_attach(GTK_GRID(container), label_adu, 0, 2, 1, 1);
	label_adu_max = gtk_label_new("ADU(max): 0");
	gtk_widget_set_margin_start_my(label_adu_max, 20);
	gtk_widget_set_margin_top(label_adu_max, 15);
	gtk_grid_attach(GTK_GRID(container), label_adu_max, 1, 2, 1, 1);
	label_mag = gtk_label_new("Magnitude(point): 0.000000");
	gtk_widget_set_margin_start_my(label_mag, 20);
	gtk_widget_set_margin_top(label_mag, 15);
	gtk_grid_attach(GTK_GRID(container), label_mag, 2, 2, 1, 1);
	label_mag_max = gtk_label_new("Magnitude(max): 0.000000");
	gtk_widget_set_margin_start_my(label_mag_max, 20);
	gtk_widget_set_margin_top(label_mag_max, 15);
	gtk_grid_attach(GTK_GRID(container), label_mag_max, 3, 2, 2, 1);
	
	char s[100];
	sprintf(s, "Image statistics: Average %.2lf, STDEV %.2lf", avg, stdev);
	label_stat = gtk_label_new(s);
	gtk_widget_set_margin_top(label_stat, 15);
	gtk_grid_attach(GTK_GRID(container), label_stat, 0, 3, 4, 1);


	buttonbox_sky = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(buttonbox_sky, 20);
	gtk_grid_attach(GTK_GRID(container), buttonbox_sky, 0, 4, 1, 1);
	button_sky = gtk_button_new_with_label("Pick sky value");
	g_signal_connect(button_sky, "clicked", G_CALLBACK(sky_pick), NULL);
	gtk_container_add(GTK_CONTAINER(buttonbox_sky), button_sky);

	label_sky = gtk_label_new("Sky value: 0.000000");
	gtk_widget_set_margin_top(label_sky, 20);
	gtk_widget_set_margin_start_my(label_sky, 20);
	gtk_widget_set_margin_end_my(label_sky, 20);
	gtk_grid_attach(GTK_GRID(container), label_sky, 1, 4, 1, 1);

	buttonbox_reset = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(buttonbox_reset, 20);
	gtk_grid_attach(GTK_GRID(container), buttonbox_reset, 2, 4, 1, 1);
	button_reset = gtk_button_new_with_label("Reset sky value");
	g_signal_connect(button_reset, "clicked", G_CALLBACK(sky_reset), NULL);
	gtk_container_add(GTK_CONTAINER(buttonbox_reset), button_reset);
	
	label_status = gtk_label_new("Status:");
	gtk_widget_set_margin_top(label_status, 20);
	gtk_widget_set_margin_bottom(label_status, 10);
	gtk_grid_attach(GTK_GRID(container), label_status, 0, 5, 1, 1);
	gtk_widget_show_all(window_sub);
	label_status = gtk_label_new("Select mode");
	gtk_widget_set_margin_top(label_status, 20);
	gtk_widget_set_margin_bottom(label_status, 10);
	gtk_grid_attach(GTK_GRID(container), label_status, 1, 5, 3, 1);
	gtk_widget_show_all(window_sub);




	window_options = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window_options), "View options");
	gtk_window_set_resizable(GTK_WINDOW(window_options), FALSE);

	container = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(window_options), container);
	gtk_widget_set_margin_start_my(container, 20);
	gtk_widget_set_margin_end_my(container, 30);

	label_zoom1 = gtk_label_new("Image zoom");
	gtk_widget_set_margin_top(label_zoom1, 20);
	gtk_widget_set_margin_bottom(label_zoom1, 20);
	gtk_grid_attach(GTK_GRID(container), label_zoom1, 0, 0, 1, 1);

	liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "12.5%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "25%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "37.5%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "50%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "62.5%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "75%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "87.5%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "100%", -1);


	combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
	gtk_widget_set_margin_top(combo, 20);
	gtk_widget_set_margin_bottom(combo, 20);
	gtk_widget_set_margin_start_my(combo, 20);
	column = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), column, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo), column, "text", NULL);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 2);
	g_signal_connect(combo, "changed", G_CALLBACK(combosel), NULL);
	gtk_grid_attach(GTK_GRID(container), combo, 1, 0, 1, 1);


	label_zoom2 = gtk_label_new("Preview zoom");
	gtk_widget_set_margin_top(label_zoom2, 20);
	gtk_widget_set_margin_bottom(label_zoom2, 20);
	gtk_widget_set_margin_start_my(label_zoom2, 20);
	gtk_grid_attach(GTK_GRID(container), label_zoom2, 2, 0, 1, 1);
	liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "50%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "100%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "150%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "200%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "250%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "300%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "350%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "400%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "450%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "500%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "550%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "600%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "650%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "700%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "750%", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "800%", -1);


	combo_prev = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
	gtk_widget_set_margin_top(combo_prev, 20);
	gtk_widget_set_margin_bottom(combo_prev, 20);
	gtk_widget_set_margin_start_my(combo_prev, 20);
	column = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo_prev), column, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_prev), column, "text", NULL);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_prev), 3);
	g_signal_connect(combo_prev, "changed", G_CALLBACK(combosel_prev), NULL);
	gtk_grid_attach(GTK_GRID(container), combo_prev, 3, 0, 1, 1);


	for(int i = 0; i < imgsize_mem; i++) {
		histogram[pixels[i]/128]++;
	}

	histoprev = gtk_drawing_area_new();
	eventbox = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(eventbox), histoprev);
	gtk_widget_set_size_request(eventbox, 512, 150);
	gtk_widget_set_margin_start_my(eventbox, 47);
	gtk_widget_set_margin_end_my(eventbox, 10);
	g_signal_connect(G_OBJECT(histoprev), "draw", G_CALLBACK(draw_histogram), NULL);
	gtk_grid_attach(GTK_GRID(container), eventbox, 0, 1, 4, 1);


	scalemin = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 65535, 1);
	gtk_widget_set_margin_top(scalemin, 0);
	gtk_widget_set_size_request(scalemin, 565, 30);
	gtk_scale_set_value_pos(GTK_SCALE(scalemin), GTK_POS_LEFT);
	gtk_range_set_value(GTK_RANGE(scalemin), avg);
	//g_signal_connect(scalemin, "value_changed", G_CALLBACK(scale), NULL);
	gtk_grid_attach(GTK_GRID(container), scalemin, 0, 2, 4, 1);

	scalemax = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 65535, 1);
	gtk_widget_set_margin_top(scalemax, 0);
	gtk_widget_set_size_request(scalemax, 565, 30);
	gtk_scale_set_value_pos(GTK_SCALE(scalemax), GTK_POS_LEFT);
	gtk_range_set_value(GTK_RANGE(scalemax), avg+stdev);
	//g_signal_connect(scalemax, "value_changed", G_CALLBACK(scale), NULL);
	gtk_grid_attach(GTK_GRID(container), scalemax, 0, 3, 4, 1);

	buttonbox_refresh = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(buttonbox_refresh, 5);
	gtk_widget_set_margin_bottom(buttonbox_refresh, 20);
	//gtk_widget_set_size_request(buttonbox_refresh, 200, 20);
	gtk_grid_attach(GTK_GRID(container), buttonbox_refresh, 0, 4, 2, 1);
	button_refresh = gtk_button_new_with_label("Reload image by new scale");
	g_signal_connect(button_refresh, "clicked", G_CALLBACK(scale), NULL);
	gtk_container_add(GTK_CONTAINER(buttonbox_refresh), button_refresh);

	buttonbox_autoscale = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(buttonbox_autoscale, 5);
	gtk_widget_set_margin_bottom(buttonbox_autoscale, 20);
	//gtk_widget_set_size_request(buttonbox_autoscale, 200, 20);
	gtk_grid_attach(GTK_GRID(container), buttonbox_autoscale, 2, 4, 1, 1);
	button_autoscale = gtk_button_new_with_label("Auto scale");
	g_signal_connect(button_autoscale, "clicked", G_CALLBACK(scale_auto), NULL);
	gtk_container_add(GTK_CONTAINER(buttonbox_autoscale), button_autoscale);

	label_ap = gtk_label_new("Aperture radius");
	gtk_widget_set_margin_top(label_ap, 0);
	gtk_widget_set_margin_bottom(label_ap, 20);
	gtk_widget_set_margin_start_my(label_ap, 40);
	gtk_grid_attach(GTK_GRID(container), label_ap, 0, 5, 1, 1);
	liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

	char buf[10];
	for(int i = 1; i <= 100; i++) {
		sprintf(buf, "%d", i);
		gtk_list_store_insert_with_values(liststore, NULL, -1, 0, buf, -1);
	}

	combo_ap = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
	gtk_widget_set_margin_top(combo_ap, 0);
	gtk_widget_set_margin_bottom(combo_ap, 20);
	gtk_widget_set_margin_start_my(combo_ap, 20);
	column = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo_ap), column, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_ap), column, "text", NULL);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_ap), 4);
	g_signal_connect(combo_ap, "changed", G_CALLBACK(combosel_ap), NULL);
	gtk_grid_attach(GTK_GRID(container), combo_ap, 1, 5, 1, 1);

	label_color = gtk_label_new("Color mode");
	gtk_widget_set_margin_top(label_color, 0);
	gtk_widget_set_margin_bottom(label_color, 20);
	gtk_widget_set_margin_start_my(label_color, 40);
	gtk_grid_attach(GTK_GRID(container), label_color, 2, 5, 1, 1);
	liststore = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);

	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "Grayscale", -1);
	gtk_list_store_insert_with_values(liststore, NULL, -1, 0, "Rainbow", -1);

	combo_color = gtk_combo_box_new_with_model(GTK_TREE_MODEL(liststore));
	gtk_widget_set_margin_top(combo_color, 0);
	gtk_widget_set_margin_bottom(combo_color, 20);
	gtk_widget_set_margin_start_my(combo_color, 20);
	column = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo_color), column, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(combo_color), column, "text", NULL);
	gtk_combo_box_set_active(GTK_COMBO_BOX(combo_color), 0);
	g_signal_connect(combo_color, "changed", G_CALLBACK(combosel_color), NULL);
	gtk_grid_attach(GTK_GRID(container), combo_color, 3, 5, 1, 1);

	gtk_widget_show_all(window_options);



	window_header = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window_header), "FITS Headers");
	gtk_window_set_default_size(GTK_WINDOW(window_header), 300, 600);

	text_header = gtk_text_view_new();
	textbuf_header = gtk_text_buffer_new(NULL);
	gtk_text_buffer_set_text(textbuf_header, headers, -1);
	gtk_text_buffer_set_modified(textbuf_header, FALSE);
	text_header = gtk_text_view_new_with_buffer(textbuf_header);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text_header), FALSE);
	gtk_widget_set_sensitive(text_header, FALSE);

	eventbox_header = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(eventbox_header), text_header);
	gtk_container_add(GTK_CONTAINER(window_header), eventbox_header);
	gtk_widget_show_all(window_header);



	window_profile = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window_profile), "Star profiles");
	gtk_window_set_default_size(GTK_WINDOW(window_profile), 512, 150);

	draw_profiles = gtk_image_new();
	eventbox = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(eventbox), draw_profiles);
	gtk_widget_set_size_request(eventbox, 512, 150);
	//g_signal_connect(G_OBJECT(draw_profiles), "draw", G_CALLBACK(draw_profiles_func), NULL);
	gtk_container_add(GTK_CONTAINER(window_profile), eventbox);
	gtk_widget_show_all(window_profile);
}

int main(int argc, char *argv[]) {
	GtkApplication *app;
	int status;
	char argv_tmp_tmp_tmp;
	char *argv_tmp_tmp = &argv_tmp_tmp_tmp;
	char **argv_tmp = &argv_tmp_tmp;

	long imgsize[100];

	fits_open_diskfile(&file, argv[1], READONLY, &status);
	if(status) fprintf(stderr, "FITS reading error\n");

	fits_get_img_size(file, 2, imgsize, &status);
	if(status) fprintf(stderr, "Image size reading error\n");
	imgx = (int)imgsize[0];
	imgy = (int)imgsize[1];
	imgsize_mem = imgx * imgy;

	char card[FLEN_CARD];
	int single = 0, hdupos, nkeys;
	fits_get_hdu_num(file, &hdupos);
	if (hdupos != 1 || strchr(argv[1], '[')) single = 1;
	for (; !status; hdupos++) {
		fits_get_hdrspace(file, &nkeys, NULL, &status);
		for (int i = 1; i <= nkeys; i++) {
			if (fits_read_record(file, i, card, &status)) break;
			sprintf(headers+strlen(headers), "%s\n", card);
		}
		if (single) break;
		fits_movrel_hdu(file, 1, NULL, &status); 
	}
	if (status == END_OF_FILE) status = 0;

	long pixeli[2] = {1, 1};
	pixels = (int*)malloc(imgsize_mem*sizeof(int));
	fits_read_pix(file, TINT, pixeli, imgsize_mem, NULL, pixels, NULL, &status);
	if(status) fprintf(stderr, "Image reading error\n");

	fits_close_file(file, &status);

	for (int i = 0; i < imgsize_mem; i++) {
		avg += (double)pixels[i];
	}
	avg /= imgsize_mem;
	for (int i = 0; i < imgsize_mem; i++) {
		stdev += pow(pixels[i]-avg, 2);
	}
	stdev /= imgsize_mem;
	stdev = sqrt(stdev);
	avg_o = avg;
	stdev_o = stdev;

	makeimg();

	makeimg_preview();

	zoom_lastx = imgx * mag / 2;
	zoom_lasty = imgy * mag / 2;

	app = gtk_application_new("com.hletrd.viewer", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(app), 0, argv_tmp);
	g_object_unref(app);
	if(status) fprintf(stderr, "GTK error\n");

	return status;
}