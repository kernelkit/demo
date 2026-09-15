/*
 * breeze -- Weather & time display with animated backgrounds.
 *
 * Touch/click anywhere to temporarily show a web page, then
 * automatically return to the weather view after 30 seconds.
 *
 * GTK3 + WebKitGTK + Cairo + libsoup3 + cJSON
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include "weather.h"
#include "animations.h"
#include "forecast.h"

/* ------------------------------------------------------------------ */
/* Application context                                                */
/* ------------------------------------------------------------------ */

typedef struct {
	/* Widgets */
	GtkWidget *window;
	GtkWidget *stack;
	GtkWidget *drawing_area;
	GtkWidget *forecast_area;
	GtkWidget *time_label;
	GtkWidget *temp_label;
	GtkWidget *desc_label;
	GtkWidget *detail_label;
	GtkWidget *web_view;
	GtkWidget *overlay_vbox;
	GtkWidget *loading_label;
	gboolean web_loading;

	/* State */
	WeatherData weather;
	AnimState anim;
	gboolean fullscreen;
	double drift_time;

	/* Configuration */
	double latitude;
	double longitude;
	char **web_urls;
	int url_count;
	int current_url;

	/* Second screen: the hours ahead */
	gboolean forecast;

	/* Run over each page once it has loaded, NULL when unset */
	char *url_script;
	double zoom;

	/* Forced weather for demos, -1 when off */
	int demo_weather;
	int demo_cover;

	/* Carousel */
	int carousel_weather;  /* Seconds to show weather (0 = no timer) */
	int carousel_url;      /* Seconds to show each URL (0 = disabled) */
	int carousel_forecast; /* Seconds to show the forecast (0 = as weather) */
	guint carousel_timer;

	/* Timer IDs */
	guint anim_timer;
	guint clock_timer;
	guint weather_timer;
	guint webview_timeout;
	guint load_timeout;
} AppContext;

static AppContext app;

/* Seconds to wait for a web page to load before giving up */
#define LOAD_TIMEOUT 20

/* ------------------------------------------------------------------ */
/* CSS styling                                                        */
/* ------------------------------------------------------------------ */

/*
 * Name the family rather than taking whatever "Sans" resolves to.  The
 * design uses weight 300 and 600, and DejaVu -- which is what a bare
 * container falls back to -- only has Book and Bold, so both silently
 * became the wrong weight.  DejaVu stays installed: Open Sans has no
 * sun, moon, or wind arrows, and Pango falls back to it per glyph.
 */
static const char *css_style = "label.overlay-time,"
                               "label.overlay-temp,"
                               "label.overlay-desc,"
                               "label.overlay-detail {"
                               "  font-family: \"Open Sans\", \"DejaVu Sans\", sans-serif;"
                               "}"
                               "label.overlay-time {"
                               "  color: white;"
                               "  font-size: 124px;"
                               "  font-weight: 300;"
                               "  letter-spacing: -3px;"
                               "  text-shadow: 0 3px 12px rgba(0,0,0,0.55);"
                               "}"
                               "label.overlay-temp {"
                               "  color: white;"
                               "  font-size: 58px;"
                               "  font-weight: 600;"
                               "  text-shadow: 0 2px 8px rgba(0,0,0,0.55);"
                               "}"
                               "label.overlay-desc {"
                               "  color: rgba(255,255,255,0.88);"
                               "  font-size: 32px;"
                               "  font-weight: 300;"
                               "  letter-spacing: 1px;"
                               "  text-shadow: 0 2px 8px rgba(0,0,0,0.55);"
                               "}"
                               "label.overlay-detail {"
                               "  color: rgba(255,255,255,0.72);"
                               "  font-size: 22px;"
                               "  font-weight: normal;"
                               "  letter-spacing: 1px;"
                               "  text-shadow: 0 1px 5px rgba(0,0,0,0.6);"
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
	gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
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

/*
 * Trade shows rarely lay on a thunderstorm to order, so --weather paints
 * one anyway.  Cloud cover comes along with the type, since the two
 * disagreeing looks worse than either.
 */
static const struct {
	const char *name;
	WeatherType type;
	int cover;
} demo_types[] = {
	{ "clear",    WEATHER_CLEAR,        0  },
	{ "partly",   WEATHER_PARTLY,       45 },
	{ "overcast", WEATHER_OVERCAST,     95 },
	{ "fog",      WEATHER_FOG,          98 },
	{ "drizzle",  WEATHER_DRIZZLE,      80 },
	{ "rain",     WEATHER_RAIN,         90 },
	{ "snow",     WEATHER_SNOW,         90 },
	{ "showers",  WEATHER_SHOWERS,      75 },
	{ "thunder",  WEATHER_THUNDERSTORM, 95 },
};

static void apply_demo_weather(void)
{
	if (app.demo_weather < 0)
		return;

	app.weather.valid = true;
	app.weather.type = (WeatherType)app.demo_weather;
	app.weather.cloudcover = app.demo_cover;
	app.weather.intensity = 0.8;
}

static void update_weather_labels(void)
{
	if (!app.weather.valid) {
		gtk_label_set_text(GTK_LABEL(app.temp_label), "--\u00B0");
		gtk_label_set_text(GTK_LABEL(app.desc_label), "No data");
		gtk_label_set_text(GTK_LABEL(app.detail_label), "");
		return;
	}

	char temp_buf[32];
	snprintf(temp_buf, sizeof(temp_buf), "%.0f\u00B0", app.weather.temperature);
	gtk_label_set_text(GTK_LABEL(app.temp_label), temp_buf);

	gtk_label_set_text(GTK_LABEL(app.desc_label), weather_description(app.weather.type));

	/* Everything secondary on one quiet line below the headline.
	 * Wind: API gives km/h, shown in m/s with an arrow and a compass
	 * point. */
	char rise[8], set[8], detail[160];
	double wind_ms = app.weather.windspeed / 3.6;

	weather_format_time(app.weather.sunrise, rise, sizeof(rise));
	weather_format_time(app.weather.sunset, set, sizeof(set));

	snprintf(detail, sizeof(detail),
	         "RH %d%%   \u00B7   %s %.0f m/s %s   \u00B7   "
	         "\u2600 %s   \u00B7   \u263D %s",
	         app.weather.humidity, weather_wind_arrow(app.weather.winddirection), wind_ms,
	         weather_wind_compass(app.weather.winddirection), rise, set);
	gtk_label_set_text(GTK_LABEL(app.detail_label), detail);
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

static gboolean on_draw_forecast(GtkWidget *widget, cairo_t *cr, gpointer data)
{
	(void)widget;
	(void)data;

	anim_draw(&app.anim, cr);
	forecast_draw(&app.weather, cr, app.anim.width, app.anim.height);
	return FALSE;
}

static void on_drawing_area_size_allocate(GtkWidget *widget, GdkRectangle *allocation, gpointer data)
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
	double dt = 0.033; /* ~30 fps */
	const gchar *shown = gtk_stack_get_visible_child_name(GTK_STACK(app.stack));

	/* Nothing to animate behind the web view */
	if (shown && g_strcmp0(shown, "web") == 0)
		return G_SOURCE_CONTINUE;

	anim_update(&app.anim, dt, &app.weather);
	if (shown && g_strcmp0(shown, "forecast") == 0)
		gtk_widget_queue_draw(app.forecast_area);
	else
		gtk_widget_queue_draw(app.drawing_area);
	return G_SOURCE_CONTINUE;
}

static gboolean on_clock_tick(gpointer data)
{
	(void)data;
	update_clock_label();

	/*
	 * Wander the text overlay slowly to spread screen burn-in.  The
	 * two axes run at unrelated rates, so the path fills an area
	 * rather than retracing one ring, and crosses the true centre
	 * regularly -- a circle of fixed radius is never centred at all.
	 * Slow enough that it is not seen moving: a whole hour to cross.
	 * Opposing margins keep the total constant, so GTK never sees a
	 * negative value or an out-of-bounds allocation.
	 */
	double radius = app.anim.height * 0.025; /* pixels, scaled to the display */
	double a = app.drift_time * 2.0 * M_PI / 3600.0;
	int dx, dy;

	app.drift_time += 1.0;
	dx = (int)(sin(a) * radius);
	dy = (int)(sin(a * 1.618) * radius); /* golden ratio: never repeats */

	gtk_widget_set_margin_start(app.overlay_vbox, (int)radius + dx);
	gtk_widget_set_margin_end(app.overlay_vbox, (int)radius - dx);
	gtk_widget_set_margin_top(app.overlay_vbox, (int)radius + dy);
	gtk_widget_set_margin_bottom(app.overlay_vbox, (int)radius - dy);

	return G_SOURCE_CONTINUE;
}

static gboolean on_weather_tick(gpointer data)
{
	(void)data;

	WeatherData fresh = weather_fetch(app.latitude, app.longitude);
	if (fresh.valid)
		app.weather = fresh;

	apply_demo_weather();
	update_weather_labels();
	return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* Web view / touch handling                                          */
/* ------------------------------------------------------------------ */

static gboolean on_webview_timeout(gpointer data);
static void load_next_url(void);
static void carousel_restart(void);
static void carousel_arm(int secs);
static void advance_page(void);
static void show_page(const char *name);

/*
 * How long the forecast screen holds the display.  It is a peer of the
 * weather screen rather than a detour from it -- the pair exist because
 * a 7" panel cannot show everything at once -- so it keeps the display
 * for as long as the weather screen unless told otherwise.
 */
static int forecast_secs(void)
{
	return app.carousel_forecast ? app.carousel_forecast : app.carousel_weather;
}

/*
 * Showing a page that is not the web view: hand over, and set the clock
 * ticking towards the next one.
 */
static void show_page(const char *name)
{
	gboolean is_forecast = g_strcmp0(name, "forecast") == 0;

	gtk_stack_set_visible_child_name(GTK_STACK(app.stack), name);

	if (app.carousel_weather)
		carousel_arm(is_forecast ? forecast_secs() : app.carousel_weather);
}

/*
 * One step around the rotation: the weather, then the hours ahead when
 * that screen is enabled, then each URL in turn.  Used by the carousel
 * timer and by a tap alike, so both agree on what comes next.
 */
static void advance_page(void)
{
	const gchar *cur = gtk_stack_get_visible_child_name(GTK_STACK(app.stack));

	if (!cur)
		return;

	if (g_strcmp0(cur, "web") == 0) {
		show_page("weather");
		return;
	}

	if (g_strcmp0(cur, "weather") == 0 && app.forecast) {
		show_page("forecast");
		return;
	}

	if (app.url_count > 0) {
		load_next_url();
		return;
	}

	/* No URLs to show, so the two weather screens are the rotation */
	show_page(app.forecast && g_strcmp0(cur, "weather") == 0 ? "forecast" : "weather");
}

static void cancel_load(void)
{
	if (app.load_timeout) {
		g_source_remove(app.load_timeout);
		app.load_timeout = 0;
	}

	if (app.web_loading) {
		webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(app.web_view));
		app.web_loading = FALSE;
	}

	gtk_widget_hide(app.loading_label);
}

/* A page that never finishes loading must not strand the display on
 * "Loading ..." -- give up and let the carousel try the next one. */
static gboolean on_load_timeout(gpointer data)
{
	(void)data;

	app.load_timeout = 0;
	cancel_load();

	if (app.carousel_weather)
		carousel_restart();

	return G_SOURCE_REMOVE;
}

static void show_web_view(void)
{
	int timeout;

	if (app.load_timeout) {
		g_source_remove(app.load_timeout);
		app.load_timeout = 0;
	}

	gtk_widget_hide(app.loading_label);
	app.web_loading = FALSE;
	gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "web");

	if (app.webview_timeout)
		g_source_remove(app.webview_timeout);

	timeout = app.carousel_url ? app.carousel_url : 30;
	app.webview_timeout = g_timeout_add_seconds(timeout, on_webview_timeout, NULL);
}

static gboolean on_webview_timeout(gpointer data)
{
	(void)data;

	show_page("weather");
	gtk_widget_hide(app.loading_label);
	app.web_loading = FALSE;
	app.webview_timeout = 0;

	return G_SOURCE_REMOVE;
}

static gboolean on_carousel_tick(gpointer data)
{
	(void)data;

	app.carousel_timer = 0;
	advance_page();
	return G_SOURCE_REMOVE;
}

static void carousel_arm(int secs)
{
	if (app.carousel_timer)
		g_source_remove(app.carousel_timer);
	app.carousel_timer = g_timeout_add_seconds(secs > 0 ? secs : 1, on_carousel_tick, NULL);
}

static void carousel_restart(void)
{
	carousel_arm(app.carousel_weather);
}

/*
 * Pages meant for a desktop rarely fit a 7" panel.  This is the hook to
 * fold away a sidebar, dismiss a banner, or hide a header: the snippet
 * runs once per page, so it has to find its own footing on a page that
 * may still be settling.  See the README for an example that keeps
 * trying until the page reacts.
 */
static void run_url_script(void)
{
	if (!app.url_script)
		return;

#if WEBKIT_CHECK_VERSION(2, 40, 0)
	webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(app.web_view), app.url_script, -1, NULL, NULL,
	                                    NULL, NULL, NULL);
#else
	webkit_web_view_run_javascript(WEBKIT_WEB_VIEW(app.web_view), app.url_script, NULL, NULL, NULL);
#endif
}

static void on_web_load_changed(WebKitWebView *web_view, WebKitLoadEvent event, gpointer data)
{
	(void)web_view;
	(void)data;

	if (event != WEBKIT_LOAD_FINISHED)
		return;

	run_url_script();

	if (app.web_loading)
		show_web_view();
}

static gboolean on_key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	(void)widget;
	(void)data;

	if (event->keyval == GDK_KEY_Escape) {
		gtk_main_quit();
		return TRUE;
	}
	return FALSE;
}

static void load_next_url(void)
{
	if (app.url_count == 0)
		return;

	const char *url = app.web_urls[app.current_url];
	app.current_url = (app.current_url + 1) % app.url_count;

	app.web_loading = TRUE;
	gtk_widget_show(app.loading_label);
	webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app.web_view), url);
	app.load_timeout = g_timeout_add_seconds(LOAD_TIMEOUT, on_load_timeout, NULL);
}

static void tapped(void)
{
	const gchar *current = gtk_stack_get_visible_child_name(GTK_STACK(app.stack));

	if (app.url_count == 0 && !app.forecast)
		return;

	if (current && g_strcmp0(current, "web") == 0) {
		/* Already showing the web view -- back to the weather */
		if (app.webview_timeout) {
			g_source_remove(app.webview_timeout);
			app.webview_timeout = 0;
		}
		show_page("weather");
		return;
	}

	if (app.web_loading) {
		/* Already loading -- cancel */
		cancel_load();

		if (app.carousel_weather)
			carousel_restart();
		return;
	}

	/* A tap takes over from the carousel until the page hands back */
	if (app.carousel_timer) {
		g_source_remove(app.carousel_timer);
		app.carousel_timer = 0;
	}

	advance_page();
}

static gboolean on_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	(void)widget;
	(void)event;
	(void)data;

	tapped();
	return TRUE;
}

static gboolean on_touch_event(GtkWidget *widget, GdkEventTouch *event, gpointer data)
{
	(void)widget;
	(void)data;

	if (event->type == GDK_TOUCH_END)
		tapped();

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
	g_signal_connect(app.drawing_area, "size-allocate", G_CALLBACK(on_drawing_area_size_allocate), NULL);

	/* Overlay labels, in three tiers: the clock, then the conditions,
	 * then everything secondary on one quiet line */
	app.time_label = gtk_label_new("--:--");
	gtk_widget_set_halign(app.time_label, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(app.time_label), "overlay-time");

	app.temp_label = gtk_label_new("--\u00B0");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.temp_label), "overlay-temp");

	app.desc_label = gtk_label_new("");
	gtk_style_context_add_class(gtk_widget_get_style_context(app.desc_label), "overlay-desc");

	app.detail_label = gtk_label_new("");
	gtk_widget_set_halign(app.detail_label, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(app.detail_label), "overlay-detail");

	/* Temperature and conditions share a line, baselines aligned */
	GtkWidget *conditions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
	gtk_widget_set_halign(conditions, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(app.temp_label, GTK_ALIGN_BASELINE);
	gtk_widget_set_valign(app.desc_label, GTK_ALIGN_BASELINE);
	gtk_box_set_baseline_position(GTK_BOX(conditions), GTK_BASELINE_POSITION_BOTTOM);
	gtk_box_pack_start(GTK_BOX(conditions), app.temp_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(conditions), app.desc_label, FALSE, FALSE, 0);

	/* Vertical box for text overlays */
	GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_halign(vbox, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);
	app.overlay_vbox = vbox;
	gtk_box_pack_start(GTK_BOX(vbox), app.time_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), conditions, FALSE, FALSE, 6);
	gtk_box_pack_start(GTK_BOX(vbox), app.detail_label, FALSE, FALSE, 22);

	/* Loading notification -- centered overlay, shown while web page loads */
	app.loading_label = gtk_label_new("Loading \u2026");
	gtk_widget_set_halign(app.loading_label, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(app.loading_label, GTK_ALIGN_CENTER);
	gtk_style_context_add_class(gtk_widget_get_style_context(app.loading_label), "overlay-notify");
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
	if (app.zoom > 0.0)
		webkit_web_view_set_zoom_level(WEBKIT_WEB_VIEW(app.web_view), app.zoom);
	g_signal_connect(app.web_view, "load-changed", G_CALLBACK(on_web_load_changed), NULL);

	/* Transparent overlay captures taps so any touch dismisses the web view.
	 * Without this, WebKit consumes all input events itself. */
	GtkWidget *dismiss = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(dismiss), FALSE);
	gtk_widget_set_hexpand(dismiss, TRUE);
	gtk_widget_set_vexpand(dismiss, TRUE);
	gtk_widget_add_events(dismiss, GDK_BUTTON_PRESS_MASK | GDK_TOUCH_MASK);
	g_signal_connect(dismiss, "button-press-event", G_CALLBACK(on_button_press), NULL);
	g_signal_connect(dismiss, "touch-event", G_CALLBACK(on_touch_event), NULL);

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
	       "  -f, --fullscreen              Run in fullscreen mode\n"
	       "  -l, --location LOCATION       City or Country,City (e.g., \"Stockholm\"\n"
	       "                                or \"Sweden,Stockholm\"), geocoded via Open-Meteo\n"
	       "      --lat LATITUDE            Latitude for weather (default: 59.3293)\n"
	       "      --lon LONGITUDE           Longitude for weather (default: 18.0686)\n"
	       "      --url URL                 Web page URL (repeatable for carousel)\n"
	       "      --carousel-weather SECS   Weather display time in carousel mode (default: 10)\n"
	       "      --carousel-url SECS       URL display time in carousel mode (default: 30)\n"
	       "      --forecast                Second screen with the next 12 hours\n"
	       "      --carousel-forecast SECS  Forecast display time (default: as weather)\n"
	       "      --url-script JS|FILE      JavaScript to run over each page once it\n"
	       "                                has loaded, inline or read from a file\n"
	       "      --zoom FACTOR             Web view zoom, e.g. 0.85 to fit more in\n"
	       "      --weather TYPE            Force a condition, for demos: clear, partly,\n"
	       "                                overcast, fog, drizzle, rain, snow, showers,\n"
	       "                                thunder\n"
	       "  -h, --help                    Show this help message\n"
	       "\n"
	       "Environment variables, used as fallbacks when options are not given:\n"
	       "\n"
	       "  FULLSCREEN                    Fullscreen mode: 1, true, yes, or on\n"
	       "  LOCATION                      Same as --location\n"
	       "  LATITUDE, LONGITUDE           Same as --lat and --lon\n"
	       "  WEB_URL                       Comma-separated list of URLs\n"
	       "  CAROUSEL_WEATHER              Same as --carousel-weather\n"
	       "  CAROUSEL_URL                  Same as --carousel-url\n"
	       "  FORECAST                      Second screen: 1, true, yes, or on\n"
	       "  CAROUSEL_FORECAST             Same as --carousel-forecast\n"
	       "  URL_SCRIPT                    Same as --url-script\n"
	       "  ZOOM                          Same as --zoom\n"
	       "  WEATHER                       Same as --weather\n"
	       "\n"
	       "Setting any carousel option enables automatic cycling between\n"
	       "weather and web views. Without carousel options, touch/click\n"
	       "manually toggles between views (cycling through URLs round-robin).\n"
	       "\n"
	       "Press Escape to exit.\n",
	       name);
}

/* Compose substitutes an empty string for variables unset in its own
 * environment, so an empty value must read as "not set". */
static const char *env_str(const char *name)
{
	const char *val = getenv(name);

	return (val && val[0]) ? val : NULL;
}

static gboolean env_bool(const char *name)
{
	const char *val = getenv(name);

	if (!val || !val[0])
		return FALSE;

	return g_ascii_strcasecmp(val, "0") != 0 && g_ascii_strcasecmp(val, "false") != 0 &&
	       g_ascii_strcasecmp(val, "no") != 0 && g_ascii_strcasecmp(val, "off") != 0;
}

/* Take the argument as a file when it names one, otherwise verbatim */
static char *load_script(const char *arg)
{
	char *body = NULL;

	if (g_file_test(arg, G_FILE_TEST_IS_REGULAR) && g_file_get_contents(arg, &body, NULL, NULL))
		return body;

	return g_strdup(arg);
}

static double parse_zoom(const char *arg)
{
	double z = atof(arg);

	if (z < 0.2 || z > 5.0) {
		fprintf(stderr, "Zoom %s out of range, using 1.0\n", arg);
		return 1.0;
	}

	return z;
}

static int parse_weather(const char *name)
{
	for (size_t i = 0; i < sizeof(demo_types) / sizeof(demo_types[0]); i++) {
		if (g_ascii_strcasecmp(name, demo_types[i].name) == 0) {
			app.demo_cover = demo_types[i].cover;
			return (int)demo_types[i].type;
		}
	}

	fprintf(stderr, "Unknown weather type \"%s\"\n", name);
	exit(1);
}

static void add_url(const char *url)
{
	app.url_count++;
	app.web_urls = realloc(app.web_urls, app.url_count * sizeof(char *));
	app.web_urls[app.url_count - 1] = strdup(url);
}

static void parse_args(int argc, char *argv[])
{
	/* Defaults from environment, then fallback */
	const char *env;
	const char *location;
	gboolean carousel_set = FALSE;

	/* Below zero until asked for, so an explicit 0 can mean "no timer" */
	app.carousel_weather = -1;
	app.carousel_url = -1;
	app.carousel_forecast = -1;

	env = env_str("LATITUDE");
	app.latitude = env ? atof(env) : 59.3293; /* Stockholm */

	env = env_str("LONGITUDE");
	app.longitude = env ? atof(env) : 18.0686;

	location = env_str("LOCATION");

	/* Parse WEB_URL: comma-separated list */
	env = env_str("WEB_URL");
	if (env) {
		char *copy = strdup(env);
		char *token = strtok(copy, ",");
		while (token) {
			/* Trim leading whitespace */
			while (*token == ' ')
				token++;
			if (*token)
				add_url(token);
			token = strtok(NULL, ",");
		}
		free(copy);
	}

	/* Carousel env vars */
	env = env_str("CAROUSEL_WEATHER");
	if (env) {
		app.carousel_weather = atoi(env);
		carousel_set = TRUE;
	}

	env = env_str("CAROUSEL_FORECAST");
	if (env) {
		app.carousel_forecast = atoi(env);
		carousel_set = TRUE;
	}

	env = env_str("CAROUSEL_URL");
	if (env) {
		app.carousel_url = atoi(env);
		carousel_set = TRUE;
	}

	app.fullscreen = env_bool("FULLSCREEN");
	app.forecast = env_bool("FORECAST");

	env = env_str("URL_SCRIPT");
	if (env)
		app.url_script = load_script(env);

	env = env_str("ZOOM");
	if (env)
		app.zoom = parse_zoom(env);

	app.demo_weather = -1;
	env = env_str("WEATHER");
	if (env)
		app.demo_weather = parse_weather(env);

	static const struct option long_opts[] = {
		{ "carousel-forecast", required_argument, NULL, 'G' },
		{ "carousel-url",      required_argument, NULL, 'c' },
		{ "carousel-weather",  required_argument, NULL, 'C' },
		{ "forecast",          no_argument,       NULL, 'F' },
		{ "fullscreen",        no_argument,       NULL, 'f' },
		{ "help",              no_argument,       NULL, 'h' },
		{ "lat",               required_argument, NULL, 'a' },
		{ "location",          required_argument, NULL, 'l' },
		{ "lon",               required_argument, NULL, 'o' },
		{ "url",               required_argument, NULL, 'u' },
		{ "url-script",        required_argument, NULL, 'S' },
		{ "weather",           required_argument, NULL, 'w' },
		{ "zoom",              required_argument, NULL, 'z' },
		{ NULL,                0,                 NULL, 0   }
	};
	int c;

	while ((c = getopt_long(argc, argv, "fhl:", long_opts, NULL)) != -1) {
		switch (c) {
		case 'a': app.latitude = atof(optarg); break;
		case 'c':
			app.carousel_url = atoi(optarg);
			carousel_set = TRUE;
			break;
		case 'C':
			app.carousel_weather = atoi(optarg);
			carousel_set = TRUE;
			break;
		case 'F': app.forecast = TRUE; break;
		case 'G':
			app.carousel_forecast = atoi(optarg);
			carousel_set = TRUE;
			break;
		case 'f': app.fullscreen = TRUE; break;
		case 'h': usage(argv[0]); exit(0);
		case 'l': location = optarg; break;
		case 'o': app.longitude = atof(optarg); break;
		case 'u': add_url(optarg); break;
		case 'S':
			g_free(app.url_script);
			app.url_script = load_script(optarg);
			break;
		case 'w': app.demo_weather = parse_weather(optarg); break;
		case 'z': app.zoom = parse_zoom(optarg); break;
		default: usage(argv[0]); exit(1);
		}
	}

	/*
	 * Asking for a second screen is asking for the display to turn
	 * itself over: nobody is going to stand there tapping, and the
	 * forecast is invisible until something moves.  So --forecast puts
	 * the display on a timer just as the carousel options do.
	 *
	 * An explicit 0 is the way out of that, and the way to a display
	 * that only ever moves when touched.
	 */
	if (app.carousel_weather < 0)
		app.carousel_weather = (carousel_set || app.forecast) ? 10 : 0;
	if (app.carousel_url < 0)
		app.carousel_url = 30;
	if (app.carousel_forecast < 0)
		app.carousel_forecast = 0; /* 0 keeps the weather screen's timing */

	if (location) {
		double lat, lon;

		if (weather_geocode(location, &lat, &lon)) {
			app.latitude = lat;
			app.longitude = lon;
			fprintf(stderr, "Location \"%s\" -> %.4f, %.4f\n", location, lat, lon);
		} else {
			fprintf(stderr, "Could not geocode \"%s\", using default coordinates\n", location);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

/*
 * gtk_window_fullscreen() asks the window manager to do the work, and a
 * kiosk started straight on top of X has no window manager to ask.  The
 * window then keeps its default size, which on a smaller panel leaves
 * the right and bottom edges hanging off the screen.  Size it to the
 * monitor ourselves; where a window manager does exist it takes over
 * and this does no harm.
 */
static void size_to_monitor(void)
{
	GdkDisplay *display = gdk_display_get_default();
	GdkMonitor *monitor;
	GdkRectangle geom;

	if (!display)
		return;

	monitor = gdk_display_get_primary_monitor(display);
	if (!monitor)
		monitor = gdk_display_get_monitor(display, 0);
	if (!monitor)
		return;

	gdk_monitor_get_geometry(monitor, &geom);
	gtk_window_move(GTK_WINDOW(app.window), geom.x, geom.y);
	gtk_window_resize(GTK_WINDOW(app.window), geom.width, geom.height);
}

static void on_realize(GtkWidget *widget, gpointer data)
{
	(void)data;
	GdkWindow *gdk_win = gtk_widget_get_window(widget);

	if (gdk_win) {
		GdkCursor *cursor =
		        gdk_cursor_new_for_display(gdk_window_get_display(gdk_win), GDK_BLANK_CURSOR);
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

	if (app.fullscreen) {
		gtk_window_set_decorated(GTK_WINDOW(app.window), FALSE);
		gtk_window_fullscreen(GTK_WINDOW(app.window));
	}

	/* Enable input events on the window */
	gtk_widget_add_events(app.window, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK | GDK_TOUCH_MASK);
	g_signal_connect(app.window, "button-press-event", G_CALLBACK(on_button_press), NULL);
	g_signal_connect(app.window, "touch-event", G_CALLBACK(on_touch_event), NULL);
	g_signal_connect(app.window, "key-press-event", G_CALLBACK(on_key_press), NULL);

	/* Stack with two children */
	app.stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(app.stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
	gtk_stack_set_transition_duration(GTK_STACK(app.stack), 500);

	GtkWidget *weather_view = create_weather_view();
	GtkWidget *web_view = create_web_view();

	app.forecast_area = gtk_drawing_area_new();
	g_signal_connect(app.forecast_area, "draw", G_CALLBACK(on_draw_forecast), NULL);

	gtk_stack_add_named(GTK_STACK(app.stack), weather_view, "weather");
	gtk_stack_add_named(GTK_STACK(app.stack), app.forecast_area, "forecast");
	gtk_stack_add_named(GTK_STACK(app.stack), web_view, "web");

	gtk_container_add(GTK_CONTAINER(app.window), app.stack);

	/* Initialize animation -- actual size comes from size-allocate */
	anim_init(&app.anim, 1024, 600); /* defaults, overridden on realize */

	/* Initial weather fetch */
	app.weather = weather_fetch(app.latitude, app.longitude);
	apply_demo_weather();
	update_weather_labels();
	update_clock_label();

	/* Start timers */
	app.anim_timer = g_timeout_add(33, on_anim_tick, NULL); /* ~30 fps */
	app.clock_timer = g_timeout_add_seconds(1, on_clock_tick, NULL);
	app.weather_timer = g_timeout_add_seconds(300, on_weather_tick, NULL); /* 5 min */

	/* Start carousel if enabled and URLs are configured */
	if (app.carousel_weather && (app.url_count > 0 || app.forecast))
		carousel_restart();

	gtk_widget_show_all(app.window);

	/* A stack ignores this until the child itself is visible, so it has
	 * to come after show_all -- otherwise the page that happens to have
	 * been added first is the one that comes up. */
	gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "weather");

	if (app.fullscreen)
		size_to_monitor();

	gtk_main();

	for (int i = 0; i < app.url_count; i++)
		free(app.web_urls[i]);
	free(app.web_urls);
	return 0;
}
