/*
 * boring -- A "boring" weather & time display with animated backgrounds.
 *
 * Touch/click anywhere to temporarily show a web page, then
 * automatically return to the weather view after 30 seconds.
 *
 * GTK3 + WebKitGTK + Cairo + libsoup3 + cJSON
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include "weather.h"
#include "animations.h"

/* ------------------------------------------------------------------ */
/* Application context                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Widgets */
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *drawing_area;
    GtkWidget *time_label;
    GtkWidget *temp_label;
    GtkWidget *desc_label;
    GtkWidget *sun_label;
    GtkWidget *web_view;

    /* State */
    WeatherData weather;
    AnimState   anim;
    gboolean    fullscreen;

    /* Configuration */
    double      latitude;
    double      longitude;
    char       *web_url;

    /* Timer IDs */
    guint       anim_timer;
    guint       clock_timer;
    guint       weather_timer;
    guint       webview_timeout;
} AppContext;

static AppContext app;

/* ------------------------------------------------------------------ */
/* CSS styling                                                        */
/* ------------------------------------------------------------------ */

static const char *css_style =
    "label.overlay-text {"
    "  color: white;"
    "  font-size: 48px;"
    "  font-weight: bold;"
    "  text-shadow: 2px 2px 6px rgba(0,0,0,0.7);"
    "}"
    "label.overlay-time {"
    "  color: white;"
    "  font-size: 96px;"
    "  font-weight: bold;"
    "  text-shadow: 3px 3px 8px rgba(0,0,0,0.7);"
    "}"
    "label.overlay-small {"
    "  color: white;"
    "  font-size: 28px;"
    "  font-weight: normal;"
    "  text-shadow: 1px 1px 4px rgba(0,0,0,0.7);"
    "}";

static void apply_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();

    gtk_css_provider_load_from_data(provider, css_style, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/* ------------------------------------------------------------------ */
/* Weather view updates                                               */
/* ------------------------------------------------------------------ */

static void update_clock_label(void)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char buf[16];

    snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
    gtk_label_set_text(GTK_LABEL(app.time_label), buf);
}

static void update_weather_labels(void)
{
    if (!app.weather.valid) {
        gtk_label_set_text(GTK_LABEL(app.temp_label), "--\u00B0C");
        gtk_label_set_text(GTK_LABEL(app.desc_label), "No data");
        gtk_label_set_text(GTK_LABEL(app.sun_label), "");
        return;
    }

    char temp_buf[32];
    snprintf(temp_buf, sizeof(temp_buf), "%.0f\u00B0C", app.weather.temperature);
    gtk_label_set_text(GTK_LABEL(app.temp_label), temp_buf);

    gtk_label_set_text(GTK_LABEL(app.desc_label),
                       weather_description(app.weather.type));

    char rise[8], set[8], sun_buf[64];
    weather_format_time(app.weather.sunrise, rise, sizeof(rise));
    weather_format_time(app.weather.sunset, set, sizeof(set));
    snprintf(sun_buf, sizeof(sun_buf), "\u2600 %s   \u263D %s", rise, set);
    gtk_label_set_text(GTK_LABEL(app.sun_label), sun_buf);
}

/* ------------------------------------------------------------------ */
/* Drawing area callback                                              */
/* ------------------------------------------------------------------ */

static gboolean on_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    (void)widget;
    (void)data;

    anim_draw(&app.anim, cr);
    return FALSE;
}

/* ------------------------------------------------------------------ */
/* Timers                                                             */
/* ------------------------------------------------------------------ */

static gboolean on_anim_tick(gpointer data)
{
    (void)data;
    double dt = 0.033;     /* ~30 fps */

    anim_update(&app.anim, dt, &app.weather);
    gtk_widget_queue_draw(app.drawing_area);
    return G_SOURCE_CONTINUE;
}

static gboolean on_clock_tick(gpointer data)
{
    (void)data;
    update_clock_label();
    return G_SOURCE_CONTINUE;
}

static gboolean on_weather_tick(gpointer data)
{
    (void)data;

    WeatherData fresh = weather_fetch(app.latitude, app.longitude);
    if (fresh.valid)
        app.weather = fresh;

    update_weather_labels();
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Web view / touch handling                                          */
/* ------------------------------------------------------------------ */

static gboolean on_webview_timeout(gpointer data)
{
    (void)data;

    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "weather");
    app.webview_timeout = 0;
    return G_SOURCE_REMOVE;
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event,
                             gpointer data)
{
    (void)widget;
    (void)data;

    if (event->keyval == GDK_KEY_Escape) {
        gtk_main_quit();
        return TRUE;
    }
    return FALSE;
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
                                gpointer data)
{
    (void)widget;
    (void)event;
    (void)data;

    if (!app.web_url || !app.web_url[0])
        return FALSE;

    const gchar *current = gtk_stack_get_visible_child_name(GTK_STACK(app.stack));

    if (current && g_strcmp0(current, "web") == 0) {
        /* Already showing web view -- go back to weather */
        if (app.webview_timeout) {
            g_source_remove(app.webview_timeout);
            app.webview_timeout = 0;
        }
        gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "weather");
        return TRUE;
    }

    /* Switch to web view */
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app.web_view), app.web_url);
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "web");

    if (app.webview_timeout)
        g_source_remove(app.webview_timeout);
    app.webview_timeout = g_timeout_add_seconds(30, on_webview_timeout, NULL);

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Widget construction                                                */
/* ------------------------------------------------------------------ */

static GtkWidget *create_weather_view(void)
{
    /* Drawing area as the background */
    app.drawing_area = gtk_drawing_area_new();
    g_signal_connect(app.drawing_area, "draw", G_CALLBACK(on_draw), NULL);

    /* Overlay labels */
    app.time_label = gtk_label_new("--:--");
    gtk_widget_set_halign(app.time_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app.time_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.time_label),
                                "overlay-time");

    app.temp_label = gtk_label_new("--\u00B0C");
    gtk_widget_set_halign(app.temp_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.temp_label),
                                "overlay-text");

    app.desc_label = gtk_label_new("");
    gtk_widget_set_halign(app.desc_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.desc_label),
                                "overlay-text");

    app.sun_label = gtk_label_new("");
    gtk_widget_set_halign(app.sun_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.sun_label),
                                "overlay-small");

    /* Vertical box for text overlays */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), app.time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.temp_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.desc_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.sun_label, FALSE, FALSE, 10);

    /* Overlay: drawing area + labels on top */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(overlay), app.drawing_area);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), vbox);

    return overlay;
}

static GtkWidget *create_web_view(void)
{
    app.web_view = webkit_web_view_new();
    return app.web_view;
}

/* ------------------------------------------------------------------ */
/* Argument parsing                                                   */
/* ------------------------------------------------------------------ */

static void usage(const char *name)
{
    printf("Usage: %s [OPTIONS]\n"
           "\n"
           "Options:\n"
           "  -f, --fullscreen         Run in fullscreen mode\n"
           "  -l, --location LOCATION  City or Country,City (e.g., \"Stockholm\"\n"
           "                           or \"Sweden,Stockholm\"), geocoded via Open-Meteo\n"
           "  --lat LATITUDE           Latitude for weather (default: 59.3293)\n"
           "  --lon LONGITUDE          Longitude for weather (default: 18.0686)\n"
           "  --url URL                Web page URL shown on touch/click\n"
           "  -h, --help               Show this help message\n"
           "\n"
           "Environment variables LATITUDE, LONGITUDE, LOCATION, and WEB_URL\n"
           "are used as fallbacks when options are not given.\n"
           "\n"
           "Press Escape to exit.\n", name);
}

static void parse_args(int argc, char *argv[])
{
    /* Defaults from environment, then fallback */
    const char *env;
    const char *location = NULL;

    env = getenv("LATITUDE");
    app.latitude = env ? atof(env) : 59.3293;       /* Stockholm */

    env = getenv("LONGITUDE");
    app.longitude = env ? atof(env) : 18.0686;

    env = getenv("LOCATION");
    if (env) location = env;

    env = getenv("WEB_URL");
    app.web_url = env ? strdup(env) : NULL;

    app.fullscreen = FALSE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 ||
            strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            exit(0);
        } else if ((strcmp(argv[i], "-l") == 0 ||
                    strcmp(argv[i], "--location") == 0) && i + 1 < argc) {
            location = argv[++i];
        } else if ((strcmp(argv[i], "--lat") == 0) && i + 1 < argc) {
            app.latitude = atof(argv[++i]);
        } else if ((strcmp(argv[i], "--lon") == 0) && i + 1 < argc) {
            app.longitude = atof(argv[++i]);
        } else if ((strcmp(argv[i], "--url") == 0) && i + 1 < argc) {
            free(app.web_url);
            app.web_url = strdup(argv[++i]);
        } else if (strcmp(argv[i], "--fullscreen") == 0 ||
                   strcmp(argv[i], "-f") == 0) {
            app.fullscreen = TRUE;
        }
    }

    if (location) {
        double lat, lon;

        if (weather_geocode(location, &lat, &lon)) {
            app.latitude = lat;
            app.longitude = lon;
            fprintf(stderr, "Location \"%s\" -> %.4f, %.4f\n",
                    location, lat, lon);
        } else {
            fprintf(stderr, "Could not geocode \"%s\", using default coordinates\n",
                    location);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

static void on_realize(GtkWidget *widget, gpointer data)
{
    (void)data;
    GdkWindow *gdk_win = gtk_widget_get_window(widget);

    if (gdk_win) {
        GdkCursor *cursor = gdk_cursor_new_for_display(
            gdk_window_get_display(gdk_win), GDK_BLANK_CURSOR);
        gdk_window_set_cursor(gdk_win, cursor);
        g_object_unref(cursor);
    }
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    parse_args(argc, argv);
    apply_css();

    /* Main window */
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Boring");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1024, 600);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Hide cursor */
    g_signal_connect(app.window, "realize", G_CALLBACK(on_realize), NULL);

    if (app.fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(app.window));

    /* Enable input events on the window */
    gtk_widget_add_events(app.window, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK);
    g_signal_connect(app.window, "button-press-event",
                     G_CALLBACK(on_button_press), NULL);
    g_signal_connect(app.window, "key-press-event",
                     G_CALLBACK(on_key_press), NULL);

    /* Stack with two children */
    app.stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(app.stack),
                                  GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(app.stack), 500);

    GtkWidget *weather_view = create_weather_view();
    GtkWidget *web_view = create_web_view();

    gtk_stack_add_named(GTK_STACK(app.stack), weather_view, "weather");
    gtk_stack_add_named(GTK_STACK(app.stack), web_view, "web");
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "weather");

    gtk_container_add(GTK_CONTAINER(app.window), app.stack);

    /* Initialize animation */
    anim_init(&app.anim, 1024, 600);

    /* Initial weather fetch */
    app.weather = weather_fetch(app.latitude, app.longitude);
    update_weather_labels();
    update_clock_label();

    /* Start timers */
    app.anim_timer    = g_timeout_add(33, on_anim_tick, NULL);           /* ~30 fps */
    app.clock_timer   = g_timeout_add_seconds(1, on_clock_tick, NULL);
    app.weather_timer = g_timeout_add_seconds(300, on_weather_tick, NULL); /* 5 min */

    gtk_widget_show_all(app.window);
    gtk_main();

    free(app.web_url);
    return 0;
}
