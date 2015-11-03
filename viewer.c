#include <gtk/gtk.h>
#ifdef __APPLE__
	#include "/usr/local/Cellar/cfitsio/3.370/include/fitsio.h"
#else
	#include "fitsio.h"
#endif
int size_x, size_y;
float scale;
char *filename;
static void activate (GtkApplication* app, gpointer user_data) {
	GtkWidget *window;
	window = gtk_application_window_new(app);
	gtk_window_set_title(GTK_WINDOW(window), "Window");
	gtk_window_set_default_size(GTK_WINDOW(window), 200, 200);
	gtk_widget_show_all(window);
}

int main(int argc, char *argv[]) {
	filename = argv[1];
	
	GtkApplication *app;
	int status;
	app = gtk_application_new("com.hletrd.gpuphotometry", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
	status = g_application_run(G_APPLICATION(app), argc, argv);
	g_object_unref(app);
	return status;
}