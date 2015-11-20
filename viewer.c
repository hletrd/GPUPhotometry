#include <gtk/gtk.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include "image.h"
#ifdef __APPLE__
	#include "/usr/local/Cellar/cfitsio/3.370/include/fitsio.h"
#else
	#include "fitsio.h"
#endif
fitsfile *file;
GtkWidget *window, *window_sub, *window_zoom;
GtkWidget *canvas, *eventbox;
GtkWidget *container;
GtkWidget *button_box1, *button1, *button_box2, *button2;
GtkWidget *label1, *label2, *label_status, *label_diff;
GtkWidget *label_adu, *label_sky, *button_sky, *buttonbox_sky, *button_reset, *buttonbox_reset, *label_stat, *label_mag;
GtkWidget *scroll, *canvas_zoom;
GdkPixbuf *pixbuf, *pixbuf_tmp, *pixbuf_full;
cairo_surface_t *surface;
cairo_t *cr;
int imgx, imgy;
int imgsize_mem;
int min, max;
float range;
float mag = 0.25;
int *pixels;
int mode = 0;
long long int sum_sky = 0;
int cnt_sky = 0;
double avg = 0, stdev = 0;
double mag1, mag2;
int viewerx, viewery;

int apsize = 5;

int adu;

static void click1(GtkWidget *widget, gpointer data) {
	gtk_label_set_text(GTK_LABEL(label_status), "Selecting reference star");
	mode = 1;
}

static void click2(GtkWidget *widget, gpointer data) {
	gtk_label_set_text(GTK_LABEL(label_status), "Selecting target star");
	mode = 2;
}

gboolean button_press_callback(GtkWidget *eventbox, GdkEventButton *event, gpointer data) {
	int x = event->x, y = event->y;
	int pixcnt = 0;
	if (viewerx == 768 || viewery == 768) {
		x -= (768 - imgx * mag) / 2;
		y -= (768 - imgy * mag) / 2;
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
	if (viewerx == 768 || viewery == 768) {
		x -= (768 - imgx * mag) / 2;
		y -= (768 - imgy * mag) / 2;
	}
	if (x < 0 || y < 0 || x > imgx * mag || y > imgy * mag) return 0;
	int pixval = 0;
	for (int i = -apsize; i <= apsize; i++) {
		for (int j = -((int)sqrt(apsize * apsize - i * i)); j*j + i*i <= apsize*apsize; j++) {
			pixval += pixels[(int)((x+i)/mag + (y+j)*imgx/mag)];
			pixcnt++;
		}
	}
	pixval /= pixcnt;
	char s[100];
	sprintf(s, "ADU at cursor: %d", pixval);
	gtk_label_set_text(GTK_LABEL(label_adu), s);
	if (cnt_sky) {
		sprintf(s, "Mag at cursor: %lf", -2.5 * log10(pixval - (double)sum_sky / cnt_sky));
	} else {
		sprintf(s, "Mag at cursor: %lf", -2.5 * log10(pixval));
	}
	if (x >= 0 && y >= 0 && x <= imgx && y <= imgy) {
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, imgx, imgy);
		cr = cairo_create(surface);
		gdk_cairo_set_source_pixbuf(cr, pixbuf, 0, 0);
		cairo_set_line_width(cr, 2.0);
		cairo_paint(cr);
		cairo_set_source_rgb(cr, 1, 1, 0);
		cairo_arc(cr, x, y, apsize*mag, 0, 2*M_PI);
		cairo_stroke(cr);
		pixbuf_tmp = gdk_pixbuf_get_from_surface(surface, 0, 0, imgx*mag, imgy*mag);
		gtk_image_set_from_pixbuf(GTK_IMAGE(canvas), pixbuf_tmp);
		cairo_surface_destroy(surface);
		cairo_destroy(cr);
		g_object_unref(pixbuf_tmp);
	}
	/*if (x >= 128*mag && y >= 128*mag && x <= imgx - 128*mag && y <= imgy - 128*mag) {
		surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 256, 256);
		cr = cairo_create(surface);
		gdk_cairo_set_source_pixbuf(cr, pixbuf_full, 0, 0);
		cairo_paint(cr);
		pixbuf_tmp = gdk_pixbuf_get_from_surface(surface, x/mag-128, y/mag-128, 256, 256);
		gtk_image_set_from_pixbuf(GTK_IMAGE(canvas_zoom), pixbuf_tmp);
		cairo_surface_destroy(surface);
		cairo_destroy(cr);
	}*/
	gtk_label_set_text(GTK_LABEL(label_mag), s);
	return 0;
}

gboolean sky_pick(GtkWidget *eventbox, GdkEventButton *event, gpointer data) {
	gtk_label_set_text(GTK_LABEL(label_status), "Selecting sky");
	mode = 3;
	return 0;
}

gboolean sky_reset(GtkWidget *eventbox, GdkEventButton *event, gpointer data) {
	sum_sky = 0;
	cnt_sky = 0;
	gtk_label_set_text(GTK_LABEL(label_sky), "Sky value: 0.000000");
	return 0;
}

void getsize(GtkWidget *widget, GtkAllocation *allocation, void *data) {
	viewerx = allocation->width;
	viewery = allocation->height;
}

static void activate(GtkApplication* app, gpointer user_data) {
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "FITS Viewer");
	gtk_window_set_default_size(GTK_WINDOW(window), 768, 768);

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
	gtk_window_set_default_size(GTK_WINDOW(window_zoom), 256, 256);
	pixbuf_full = gdk_pixbuf_new_from_file("tmp_full.bmp", error);
	unlink("tmp_full.bmp");
	canvas_zoom = gtk_image_new();
	gtk_widget_set_size_request(canvas_zoom, 256, 256);
	gtk_container_add(GTK_CONTAINER(window_zoom), canvas_zoom);
	gtk_widget_show_all(window_zoom);



	window_sub = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window_sub), "Informations");
	gtk_window_set_default_size(GTK_WINDOW(window_sub), 430, 200);

	container = gtk_grid_new();
	gtk_container_add(GTK_CONTAINER(window_sub), container);
	gtk_widget_set_margin_start(container, 20);
	gtk_widget_set_margin_end(container, 20);

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


	label_adu = gtk_label_new("ADU at cursor: 0");
	gtk_widget_set_margin_top(label_adu, 15);
	gtk_grid_attach(GTK_GRID(container), label_adu, 0, 2, 2, 1);
	label_mag = gtk_label_new("Mag at cursor: 0.000000");
	gtk_widget_set_margin_top(label_mag, 15);
	gtk_grid_attach(GTK_GRID(container), label_mag, 2, 2, 2, 1);
	
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
	gtk_widget_set_margin_start(label_sky, 20);
	gtk_widget_set_margin_end(label_sky, 20);
	gtk_grid_attach(GTK_GRID(container), label_sky, 1, 4, 1, 1);

	buttonbox_reset = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_widget_set_margin_top(buttonbox_reset, 20);
	gtk_grid_attach(GTK_GRID(container), buttonbox_reset, 2, 4, 1, 1);
	button_reset = gtk_button_new_with_label("Reset sky value");
	g_signal_connect(button_reset, "clicked", G_CALLBACK(sky_reset), NULL);
	gtk_container_add(GTK_CONTAINER(buttonbox_reset), button_reset);


	label_status = gtk_label_new("Select mode");
	gtk_widget_set_margin_top(label_status, 10);
	gtk_widget_set_margin_bottom(label_status, 10);
	gtk_grid_attach(GTK_GRID(container), label_status, 0, 5, 4, 1);

	gtk_widget_show_all(window_sub);
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

	long pixeli[2] = {1, 1};
	pixels = (int*)malloc(imgsize_mem*sizeof(int));
	fits_read_pix(file, TINT, pixeli, imgsize_mem, NULL, pixels, NULL, &status);
	if(status) fprintf(stderr, "Image reading error\n");

	for (int i = 0; i < imgsize_mem; i++) {
		avg += (double)pixels[i];
	}
	avg /= imgsize_mem;
	for (int i = 0; i < imgsize_mem; i++) {
		stdev += pow(pixels[i]-avg, 2);
	}
	stdev /= imgsize_mem;
	stdev = sqrt(stdev);
	range = stdev;
	min = (int)avg - (int)(stdev);

	if (argc >= 3) mag = atof(argv[2]);
	if (argc >= 4) apsize = atoi(argv[3]);


	struct image bmp;
	int pixelval;
	newImage(&bmp, (int)(imgx * mag), (int)(imgy * mag));
	for (int i = 0; i < imgy * mag; i++) {
		for (int j = 0; j < imgx * mag; j++) {
			pixelval = (double)((pixels[(int)(i/mag*imgx+j/mag)] - min) / range / 3 * 256);
			setPixelData(&bmp, j, (int)imgy*mag-i, pixelval);		
		}
	}
	unlink("tmp.bmp");
	FILE *tmp = fopen("tmp.bmp", "w");
	saveImage(bmp, tmp);
	unloadImage(&bmp);
	fclose(tmp);

	newImage(&bmp, (int)(imgx), (int)(imgy));
	for (int i = 0; i < imgy; i++) {
		for (int j = 0; j < imgx; j++) {
			pixelval = (double)((pixels[(int)(i*imgx+j)] - min) / range / 3 * 256);
			setPixelData(&bmp, j, (int)imgy-i, pixelval);		
		}
	}
	unlink("tmp_full.bmp");
	tmp = fopen("tmp_full.bmp", "w");
	saveImage(bmp, tmp);
	unloadImage(&bmp);
	fclose(tmp);

	app = gtk_application_new("com.hletrd.viewer", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(app), 0, argv_tmp);
	g_object_unref(app);
	if(status) fprintf(stderr, "GTK error\n");

	return status;
}