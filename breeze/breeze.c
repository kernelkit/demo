/*
 * breeze -- Weather & time display with animated backgrounds.
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
    GtkWidget *wind_label;
    GtkWidget *sun_label;
    GtkWidget *web_view;
    GtkWidget *overlay_vbox;
    GtkWidget *loading_label;
    gboolean   web_loading;

    /* State */
    WeatherData weather;
    AnimState   anim;
    gboolean    fullscreen;
    double      drift_time;

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
    "}"
    "label.overlay-notify {"
    "  color: #333;"
    "  font-size: 32px;"
    "  font-weight: bold;"
    "  background: rgba(255,200,50,0.9);"
    "  border: 2px solid rgba(220,140,20,0.9);"
    "  border-radius: 12px;"
    "  padding: 12px 32px;"
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
        gtk_label_set_text(GTK_LABEL(app.wind_label), "");
        gtk_label_set_text(GTK_LABEL(app.sun_label), "");
        return;
    }

    char temp_buf[64];
    snprintf(temp_buf, sizeof(temp_buf), "%.0f\u00B0C   RH %d%%",
             app.weather.temperature, app.weather.humidity);
    gtk_label_set_text(GTK_LABEL(app.temp_label), temp_buf);

    gtk_label_set_text(GTK_LABEL(app.desc_label),
                       weather_description(app.weather.type));

    /* Wind: API gives km/h, display in m/s with direction arrow and compass */
    double wind_ms = app.weather.windspeed / 3.6;
    char wind_buf[64];
    snprintf(wind_buf, sizeof(wind_buf), "%s %.0f m/s %s",
             weather_wind_arrow(app.weather.winddirection),
             wind_ms,
             weather_wind_compass(app.weather.winddirection));
    gtk_label_set_text(GTK_LABEL(app.wind_label), wind_buf);

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

static void on_drawing_area_size_allocate(GtkWidget *widget,
                                          GdkRectangle *allocation,
                                          gpointer data)
{
    (void)widget;
    (void)data;

    app.anim.width = allocation->width;
    app.anim.height = allocation->height;
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

    /* Slow circular drift of text overlay to prevent screen burn-in.
     * Full cycle ~5 minutes, radius ~15 pixels -- barely noticeable.
     * Use opposing margins so the total stays constant and GTK
     * never sees a negative value or an out-of-bounds allocation. */
    app.drift_time += 1.0;
    double period = 300.0;   /* seconds per full circle */
    double radius = 15.0;    /* pixels */
    int dx = (int)(sin(app.drift_time * 2.0 * M_PI / period) * radius);
    int dy = (int)(cos(app.drift_time * 2.0 * M_PI / period) * radius);

    gtk_widget_set_margin_start(app.overlay_vbox,  (int)radius + dx);
    gtk_widget_set_margin_end(app.overlay_vbox,    (int)radius - dx);
    gtk_widget_set_margin_top(app.overlay_vbox,    (int)radius + dy);
    gtk_widget_set_margin_bottom(app.overlay_vbox, (int)radius - dy);

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

static gboolean on_webview_timeout(gpointer data);

static void show_web_view(void)
{
    gtk_widget_hide(app.loading_label);
    app.web_loading = FALSE;
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "web");

    if (app.webview_timeout)
        g_source_remove(app.webview_timeout);
    app.webview_timeout = g_timeout_add_seconds(30, on_webview_timeout, NULL);
}

static gboolean on_webview_timeout(gpointer data)
{
    (void)data;

    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "weather");
    gtk_widget_hide(app.loading_label);
    app.web_loading = FALSE;
    app.webview_timeout = 0;
    return G_SOURCE_REMOVE;
}

static void on_web_load_changed(WebKitWebView *web_view,
                                WebKitLoadEvent event, gpointer data)
{
    (void)web_view;
    (void)data;

    if (event == WEBKIT_LOAD_FINISHED && app.web_loading)
        show_web_view();
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

static void toggle_web_view(void)
{
    if (!app.web_url || !app.web_url[0])
        return;

    const gchar *current = gtk_stack_get_visible_child_name(GTK_STACK(app.stack));

    if (current && g_strcmp0(current, "web") == 0) {
        /* Already showing web view -- go back to weather */
        if (app.webview_timeout) {
            g_source_remove(app.webview_timeout);
            app.webview_timeout = 0;
        }
        gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "weather");
        return;
    }

    if (app.web_loading) {
        /* Already loading -- cancel */
        webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(app.web_view));
        gtk_widget_hide(app.loading_label);
        app.web_loading = FALSE;
        return;
    }

    /* Start loading, stay on weather view until page is ready */
    app.web_loading = TRUE;
    gtk_widget_show(app.loading_label);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app.web_view), app.web_url);
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event,
                                gpointer data)
{
    (void)widget;
    (void)event;
    (void)data;

    toggle_web_view();
    return TRUE;
}

static gboolean on_touch_event(GtkWidget *widget, GdkEventTouch *event,
                               gpointer data)
{
    (void)widget;
    (void)data;

    if (event->type == GDK_TOUCH_END)
        toggle_web_view();

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
    g_signal_connect(app.drawing_area, "size-allocate",
                     G_CALLBACK(on_drawing_area_size_allocate), NULL);

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

    app.wind_label = gtk_label_new("");
    gtk_widget_set_halign(app.wind_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.wind_label),
                                "overlay-small");

    app.sun_label = gtk_label_new("");
    gtk_widget_set_halign(app.sun_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.sun_label),
                                "overlay-small");

    /* Vertical box for text overlays */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
    app.overlay_vbox = vbox;
    gtk_box_pack_start(GTK_BOX(vbox), app.time_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.temp_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.desc_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.wind_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.sun_label, FALSE, FALSE, 10);

    /* Loading notification -- centered overlay, shown while web page loads */
    app.loading_label = gtk_label_new("Loading \u2026");
    gtk_widget_set_halign(app.loading_label, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app.loading_label, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(app.loading_label),
                                "overlay-notify");
    gtk_widget_set_no_show_all(app.loading_label, TRUE);

    /* Overlay: drawing area + labels on top */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(overlay), app.drawing_area);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), vbox);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), app.loading_label);

    return overlay;
}

static GtkWidget *create_web_view(void)
{
    app.web_view = webkit_web_view_new();
    g_signal_connect(app.web_view, "load-changed",
                     G_CALLBACK(on_web_load_changed), NULL);

    /* Transparent overlay captures taps so any touch dismisses the web view.
     * Without this, WebKit consumes all input events itself. */
    GtkWidget *dismiss = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(dismiss), FALSE);
    gtk_widget_set_hexpand(dismiss, TRUE);
    gtk_widget_set_vexpand(dismiss, TRUE);
    gtk_widget_add_events(dismiss, GDK_BUTTON_PRESS_MASK | GDK_TOUCH_MASK);
    g_signal_connect(dismiss, "button-press-event",
                     G_CALLBACK(on_button_press), NULL);
    g_signal_connect(dismiss, "touch-event",
                     G_CALLBACK(on_touch_event), NULL);

    GtkWidget *overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(overlay), app.web_view);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), dismiss);

    return overlay;
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
    gtk_window_set_title(GTK_WINDOW(app.window), "Breeze");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 1024, 600);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    /* Hide cursor */
    g_signal_connect(app.window, "realize", G_CALLBACK(on_realize), NULL);

    if (app.fullscreen)
        gtk_window_fullscreen(GTK_WINDOW(app.window));

    /* Enable input events on the window */
    gtk_widget_add_events(app.window,
                          GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK |
                          GDK_TOUCH_MASK);
    g_signal_connect(app.window, "button-press-event",
                     G_CALLBACK(on_button_press), NULL);
    g_signal_connect(app.window, "touch-event",
                     G_CALLBACK(on_touch_event), NULL);
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

    /* Initialize animation -- actual size comes from size-allocate */
    anim_init(&app.anim, 1024, 600);  /* defaults, overridden on realize */

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
