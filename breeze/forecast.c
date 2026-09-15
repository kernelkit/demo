/*
 * The hours ahead: a temperature curve over a row of columns, one per
 * hour, with the chance of rain underneath and a glyph on top saying
 * what sort of hour it is.  Drawn over the animated sky, so it is kept
 * to a translucent panel rather than a page of its own.
 */

#include <math.h>
#include <stdio.h>

#include <pango/pangocairo.h>

#include "forecast.h"

#define PANEL_MARGIN 0.05 /* of width */

static void rounded_box(cairo_t *cr, double x, double y, double w, double h, double r)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(cr, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(cr, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(cr, x + r, y + r, r, M_PI, 3 * M_PI / 2);
	cairo_close_path(cr);
}

/* Pango rather than cairo's toy text, to match the rest of the display */
static void text_at(cairo_t *cr, double cx, double y, const char *str, double size, gboolean bold,
                    double alpha)
{
	PangoLayout *layout = pango_cairo_create_layout(cr);
	PangoFontDescription *desc;
	char font[64];
	int w, h;

	snprintf(font, sizeof(font), "Open Sans, DejaVu Sans %s %.0f", bold ? "Semibold" : "", size);
	desc = pango_font_description_from_string(font);
	pango_layout_set_font_description(layout, desc);
	pango_font_description_free(desc);

	pango_layout_set_text(layout, str, -1);
	pango_layout_get_pixel_size(layout, &w, &h);

	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, alpha);
	cairo_move_to(cr, cx - w / 2.0, y);
	pango_cairo_show_layout(cr, layout);
	g_object_unref(layout);
}

/*
 * A glyph per hour, drawn rather than typed: the symbol fonts disagree
 * about which of these exist, and these have to sit in a 20 pixel box.
 */
static void draw_glyph(cairo_t *cr, double cx, double cy, double s, WeatherType type, gboolean night)
{
	gboolean rain = (type == WEATHER_RAIN || type == WEATHER_DRIZZLE || type == WEATHER_SHOWERS ||
	                 type == WEATHER_THUNDERSTORM);
	gboolean snow = (type == WEATHER_SNOW);
	gboolean clear = (type == WEATHER_CLEAR);
	gboolean cloud = !clear;

	if (clear || type == WEATHER_PARTLY) {
		double ox = (type == WEATHER_PARTLY) ? -s * 0.28 : 0.0;
		double oy = (type == WEATHER_PARTLY) ? -s * 0.22 : 0.0;

		if (night) {
			/* A crescent: same trick as the sky's moon */
			cairo_save(cr);
			cairo_translate(cr, cx + ox, cy + oy);
			cairo_set_source_rgba(cr, 0.92, 0.94, 1.0, 0.95);
			cairo_arc(cr, 0, 0, s * 0.42, 0, 2 * M_PI);
			cairo_fill(cr);
			cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
			cairo_arc(cr, s * 0.22, -s * 0.12, s * 0.38, 0, 2 * M_PI);
			cairo_fill(cr);
			cairo_restore(cr);
		} else {
			cairo_set_source_rgba(cr, 1.0, 0.85, 0.35, 0.95);
			cairo_arc(cr, cx + ox, cy + oy, s * 0.40, 0, 2 * M_PI);
			cairo_fill(cr);
		}
	}

	if (cloud) {
		double w = s * 0.92, y = cy + s * 0.12;
		double grey = (type == WEATHER_OVERCAST || rain || snow) ? 0.72 : 0.88;

		cairo_set_source_rgba(cr, grey, grey, grey * 1.04, 0.95);
		cairo_arc(cr, cx - w * 0.26, y, s * 0.26, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_arc(cr, cx + w * 0.02, y - s * 0.12, s * 0.32, 0, 2 * M_PI);
		cairo_fill(cr);
		cairo_arc(cr, cx + w * 0.30, y, s * 0.24, 0, 2 * M_PI);
		cairo_fill(cr);
		rounded_box(cr, cx - w * 0.46, y - s * 0.02, w * 0.92, s * 0.30, s * 0.14);
		cairo_fill(cr);
	}

	if (rain) {
		cairo_set_source_rgba(cr, 0.62, 0.78, 1.0, 0.95);
		cairo_set_line_width(cr, s * 0.10);
		for (int i = -1; i <= 1; i++) {
			double x = cx + i * s * 0.26;

			cairo_move_to(cr, x + s * 0.06, cy + s * 0.46);
			cairo_line_to(cr, x - s * 0.04, cy + s * 0.72);
		}
		cairo_stroke(cr);
	}

	if (snow) {
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
		for (int i = -1; i <= 1; i++) {
			cairo_arc(cr, cx + i * s * 0.26, cy + s * 0.58, s * 0.07, 0, 2 * M_PI);
			cairo_fill(cr);
		}
	}
}

void forecast_draw(const WeatherData *weather, cairo_t *cr, int width, int height)
{
	double m = width * PANEL_MARGIN;
	double x = m, w = width - 2 * m;
	double y = height * 0.16, h = height * 0.68;
	double slot = w / (weather->forecast_count ? weather->forecast_count : 1);
	double lo = 1e9, hi = -1e9;
	double curve_top = y + h * 0.42, curve_bot = y + h * 0.64;
	double bar_base = y + h * 0.90;
	double scale = height / 600.0;
	int n = weather->forecast_count;

	if (n < 2) {
		text_at(cr, width / 2.0, height * 0.46, "No forecast", 26 * scale, FALSE, 0.85);
		return;
	}

	/* The panel, so the curve is legible over any sky */
	rounded_box(cr, x, y, w, h, 18 * scale);
	cairo_set_source_rgba(cr, 0.06, 0.08, 0.14, 0.52);
	cairo_fill(cr);

	text_at(cr, width / 2.0, y + h * 0.02, "Next 12 hours", 19 * scale, TRUE, 0.80);

	for (int i = 0; i < n; i++) {
		double t = weather->forecast[i].temperature;

		if (t < lo)
			lo = t;
		if (t > hi)
			hi = t;
	}
	if (hi - lo < 2.0) { /* a flat day still wants a curve, not a line */
		double mid = (hi + lo) / 2.0;

		lo = mid - 1.0;
		hi = mid + 1.0;
	}

	/* Chance of rain, as a column behind everything else */
	for (int i = 0; i < n; i++) {
		const ForecastHour *f = &weather->forecast[i];
		double cx = x + slot * (i + 0.5);
		double bh = (bar_base - curve_bot - 6 * scale) * (f->precip_prob / 100.0);

		if (f->precip_prob < 5)
			continue;

		rounded_box(cr, cx - slot * 0.22, bar_base - bh, slot * 0.44, bh, 3 * scale);
		cairo_set_source_rgba(cr, 0.42, 0.66, 0.95, 0.42);
		cairo_fill(cr);
	}

	/* Temperature curve */
	cairo_set_line_width(cr, 2.5 * scale);
	cairo_set_source_rgba(cr, 1.0, 0.82, 0.35, 0.95);
	for (int i = 0; i < n; i++) {
		const ForecastHour *f = &weather->forecast[i];
		double cx = x + slot * (i + 0.5);
		double cy = curve_bot - (f->temperature - lo) / (hi - lo) * (curve_bot - curve_top);

		if (i == 0)
			cairo_move_to(cr, cx, cy);
		else
			cairo_line_to(cr, cx, cy);
	}
	cairo_stroke(cr);

	for (int i = 0; i < n; i++) {
		const ForecastHour *f = &weather->forecast[i];
		double cx = x + slot * (i + 0.5);
		double cy = curve_bot - (f->temperature - lo) / (hi - lo) * (curve_bot - curve_top);
		gboolean night = f->hour < 6 || f->hour >= 21;
		char buf[16];

		cairo_set_source_rgba(cr, 1.0, 0.82, 0.35, 1.0);
		cairo_arc(cr, cx, cy, 3.0 * scale, 0, 2 * M_PI);
		cairo_fill(cr);

		snprintf(buf, sizeof(buf), "%.0f°", f->temperature);
		text_at(cr, cx, cy - 26 * scale, buf, 16 * scale, TRUE, 0.95);

		/*
		 * The hour heads its own column, above the glyph.  Along the
		 * bottom it sat a few pixels from the millimetres and the two
		 * read as one another.
		 */
		snprintf(buf, sizeof(buf), "%02d", f->hour);
		text_at(cr, cx, y + h * 0.12, buf, 16 * scale, FALSE, 0.85);

		draw_glyph(cr, cx, y + h * 0.26, 22 * scale, f->type, night);

		/* Nothing else lives down here now, so a number can only be mm */
		if (f->precipitation >= 0.05) {
			snprintf(buf, sizeof(buf), "%.1f", f->precipitation);
			text_at(cr, cx, bar_base + 4 * scale, buf, 13 * scale, FALSE, 0.80);
		}
	}
}
