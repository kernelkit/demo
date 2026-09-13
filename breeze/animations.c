#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "animations.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static double randf(void)
{
    return (double)rand() / (double)RAND_MAX;
}

static double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

static double clampf(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* ------------------------------------------------------------------ */
/* Sky                                                                */
/* ------------------------------------------------------------------ */

/*
 * The sky is keyed on the sun's altitude rather than on a day/night
 * flag, so dawn and dusk get their own colours instead of the sky
 * snapping between blue and black.  Each key is a three stop gradient,
 * zenith to horizon, and the sky in between is interpolated.
 */
typedef struct {
    double alt;
    double top[3];
    double mid[3];
    double bot[3];
} SkyKey;

static const SkyKey sky_keys[] = {
    { -1.00, { 0.01, 0.02, 0.07 }, { 0.02, 0.04, 0.12 }, { 0.04, 0.06, 0.16 } },
    { -0.25, { 0.02, 0.04, 0.13 }, { 0.06, 0.09, 0.23 }, { 0.13, 0.13, 0.29 } },
    { -0.10, { 0.05, 0.09, 0.26 }, { 0.17, 0.18, 0.40 }, { 0.40, 0.28, 0.45 } },
    {  0.00, { 0.13, 0.20, 0.44 }, { 0.52, 0.34, 0.44 }, { 0.98, 0.56, 0.30 } },
    {  0.12, { 0.20, 0.38, 0.70 }, { 0.55, 0.58, 0.72 }, { 0.98, 0.76, 0.52 } },
    {  0.40, { 0.16, 0.38, 0.78 }, { 0.35, 0.60, 0.88 }, { 0.66, 0.82, 0.94 } },
    {  1.00, { 0.10, 0.32, 0.76 }, { 0.30, 0.58, 0.90 }, { 0.62, 0.80, 0.96 } },
};

#define SKY_KEYS (int)(sizeof(sky_keys) / sizeof(sky_keys[0]))

/* Overcast drains the colour out of whatever the sky would have been */
static void sky_stop(const AnimState *state, int stop, double rgb[3])
{
    double cover = state->weather.cloudcover / 100.0;
    const SkyKey *a = &sky_keys[0];
    const SkyKey *b = &sky_keys[SKY_KEYS - 1];
    double t = 0.0;

    for (int i = 0; i < SKY_KEYS - 1; i++) {
	if (state->sun_alt <= sky_keys[i + 1].alt) {
	    a = &sky_keys[i];
	    b = &sky_keys[i + 1];
	    t = (state->sun_alt - a->alt) / (b->alt - a->alt);
	    break;
	}
    }
    t = clampf(t, 0.0, 1.0);

    const double *ca = stop == 0 ? a->top : (stop == 1 ? a->mid : a->bot);
    const double *cb = stop == 0 ? b->top : (stop == 1 ? b->mid : b->bot);

    for (int i = 0; i < 3; i++)
	rgb[i] = lerp(ca[i], cb[i], t);

    double grey = (rgb[0] + rgb[1] + rgb[2]) / 3.0;
    for (int i = 0; i < 3; i++)
	rgb[i] = lerp(rgb[i], grey, cover * 0.55);
}

/*
 * Moon phase from the synodic month, counting from the new moon of
 * 2000-01-06.  Good to a few hours, which is a lot finer than the
 * eight shapes anyone can tell apart on a display.
 */
static double moon_phase(time_t now)
{
    double days = (double)(now - 947182440) / 86400.0;
    double phase = fmod(days / 29.530588853, 1.0);

    return phase < 0 ? phase + 1.0 : phase;
}

static void update_sky(AnimState *state)
{
    time_t now = time(NULL);
    struct tm tm;
    double hour, rise, set, frac, theta, span;

    localtime_r(&now, &tm);
    hour = tm.tm_hour + tm.tm_min / 60.0 + tm.tm_sec / 3600.0;

    state->horizon = state->height * 0.82;
    state->moon_phase = moon_phase(now);

    rise = state->weather.sunrise;
    set  = state->weather.sunset;
    if (set - rise < 0.5) {     /* no weather data yet, or polar day */
	rise = 6.0;
	set  = 18.0;
    }

    state->sun_alpha = 0.0;
    state->moon_alpha = 0.0;

    if (hour >= rise && hour <= set) {
	frac = (hour - rise) / (set - rise);
	theta = M_PI * frac;
	state->sun_alt = sin(theta);
	state->sun_alpha = clampf(sin(theta) * 12.0, 0.0, 1.0);
	state->sun_x = state->width * (0.10 + 0.80 * frac);
	state->sun_y = state->horizon -
	    sin(theta) * (state->horizon - state->height * 0.12);
    } else {
	span = 24.0 - (set - rise);
	frac = (hour > set ? hour - set : hour + 24.0 - set) / span;
	theta = M_PI * frac;
	state->sun_alt = -sin(theta);
	state->moon_alpha = clampf(sin(theta) * 8.0, 0.0, 1.0);
	state->moon_x = state->width * (0.10 + 0.80 * frac);
	state->moon_y = state->horizon -
	    sin(theta) * (state->horizon - state->height * 0.16);
    }
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                     */
/* ------------------------------------------------------------------ */

void anim_init(AnimState *state, int width, int height)
{
    memset(state, 0, sizeof(*state));
    state->width = width;
    state->height = height;

    /* Seed with something */
    srand(42);

    /* Stars sit above the horizon, brighter ones sparser */
    state->star_count = ANIM_MAX_STARS;
    for (int i = 0; i < state->star_count; i++) {
	Star *st = &state->stars[i];
	double b = randf();

	st->x = randf() * width;
	st->y = randf() * height * 0.80;
	st->brightness = 0.25 + b * b * 0.75;
	st->twinkle_phase = randf() * M_PI * 2.0;
    }

    /* Pre-place a few clouds */
    for (int i = 0; i < ANIM_MAX_CLOUDS; i++) {
        Cloud *c = &state->clouds[i];

        c->x       = randf() * width;
        c->y       = randf() * height * 0.35;
        c->speed   = 8.0 + randf() * 20.0;
        c->size    = 40.0 + randf() * 60.0;
        c->opacity = 0.25 + randf() * 0.35;
    }
}

/* ------------------------------------------------------------------ */
/* Update                                                             */
/* ------------------------------------------------------------------ */

static void update_clouds(AnimState *state, double dt)
{
    /* Target cloud count proportional to cloud cover */
    int target = (int)(state->weather.cloudcover / 100.0 * ANIM_MAX_CLOUDS);

    if (target < 2)
        target = 2;
    state->cloud_count = target;

    for (int i = 0; i < state->cloud_count; i++) {
        Cloud *c = &state->clouds[i];

        c->x += c->speed * dt;
        if (c->x - c->size > state->width) {
            c->x = -c->size * 2;
            c->y = randf() * state->height * 0.35;
            c->speed = 8.0 + randf() * 20.0;
        }
    }
}

static void update_particles(AnimState *state, double dt)
{
    bool rain = (state->weather.type == WEATHER_RAIN ||
                 state->weather.type == WEATHER_DRIZZLE ||
                 state->weather.type == WEATHER_SHOWERS ||
                 state->weather.type == WEATHER_THUNDERSTORM);
    bool snow = (state->weather.type == WEATHER_SNOW);

    if (!rain && !snow) {
        state->particle_count = 0;
        return;
    }

    int target = (int)(state->weather.intensity * ANIM_MAX_PARTICLES);

    if (target < 5)
        target = 5;
    if (target > ANIM_MAX_PARTICLES)
        target = ANIM_MAX_PARTICLES;

    /* Spawn new particles if needed */
    while (state->particle_count < target) {
        Particle *p = &state->particles[state->particle_count];

        p->x            = randf() * state->width;
        p->y            = -randf() * state->height * 0.3;
        p->speed        = snow ? (30.0 + randf() * 40.0) : (200.0 + randf() * 300.0);
        p->wobble_phase = randf() * M_PI * 2.0;
        p->size         = snow ? (2.0 + randf() * 3.0) : (1.0 + randf() * 1.5);
        state->particle_count++;
    }

    /* Remove excess */
    if (state->particle_count > target)
        state->particle_count = target;

    for (int i = 0; i < state->particle_count; i++) {
        Particle *p = &state->particles[i];

        p->y += p->speed * dt;

        if (snow) {
            p->wobble_phase += dt * 2.0;
            p->x += sin(p->wobble_phase) * 20.0 * dt;
        }

        /* Wrap around at bottom */
        if (p->y > state->height) {
            p->y = -10.0;
            p->x = randf() * state->width;
        }
    }
}

static void update_streaks(AnimState *state, double dt)
{
    /* Wind streaks appear above ~5 m/s, scale up to max at ~15 m/s */
    double wind_ms = state->weather.windspeed / 3.6;
    int target = 0;

    if (wind_ms >= 5.0) {
        double frac = (wind_ms - 5.0) / 10.0;

        if (frac > 1.0) frac = 1.0;
        target = (int)(frac * ANIM_MAX_STREAKS);
        if (target < 1) target = 1;
    }

    while (state->streak_count < target) {
        Particle *s = &state->streaks[state->streak_count];

        s->x    = -randf() * state->width * 0.3;
        s->y    = randf() * state->height;
        s->speed = 150.0 + wind_ms * 20.0 + randf() * 100.0;
        s->size  = 30.0 + randf() * 50.0;  /* streak length */
        state->streak_count++;
    }

    if (state->streak_count > target)
        state->streak_count = target;

    for (int i = 0; i < state->streak_count; i++) {
        Particle *s = &state->streaks[i];

        s->x += s->speed * dt;

        if (s->x > state->width + s->size) {
            s->x = -s->size - randf() * state->width * 0.2;
            s->y = randf() * state->height;
            s->speed = 150.0 + wind_ms * 20.0 + randf() * 100.0;
        }
    }
}

void anim_update(AnimState *state, double dt, const WeatherData *weather)
{
    state->weather = *weather;
    state->time_accum += dt;

    update_sky(state);
    update_clouds(state, dt);
    update_particles(state, dt);
    update_streaks(state, dt);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static void draw_sky(const AnimState *state, cairo_t *cr)
{
    double top[3], mid[3], bot[3];
    cairo_pattern_t *grad;

    sky_stop(state, 0, top);
    sky_stop(state, 1, mid);
    sky_stop(state, 2, bot);

    grad = cairo_pattern_create_linear(0, 0, 0, state->height);
    cairo_pattern_add_color_stop_rgb(grad, 0.00, top[0], top[1], top[2]);
    cairo_pattern_add_color_stop_rgb(grad, 0.55, mid[0], mid[1], mid[2]);
    cairo_pattern_add_color_stop_rgb(grad, 1.00, bot[0], bot[1], bot[2]);

    cairo_set_source(cr, grad);
    cairo_paint(cr);
    cairo_pattern_destroy(grad);
}

static void draw_stars(const AnimState *state, cairo_t *cr)
{
    double cover = state->weather.cloudcover / 100.0;
    double fade = clampf((-state->sun_alt - 0.02) * 4.0, 0.0, 1.0) *
	          (1.0 - cover * 0.75);

    if (fade <= 0.01)
	return;

    for (int i = 0; i < state->star_count; i++) {
	const Star *st = &state->stars[i];
	double tw = 0.65 + 0.35 * sin(state->time_accum * 1.7 + st->twinkle_phase);
	double a = st->brightness * tw * fade;
	double r = st->brightness * 1.3;

	cairo_set_source_rgba(cr, 1.0, 0.98, 0.92, a);
	cairo_arc(cr, st->x, st->y, r, 0, 2.0 * M_PI);
	cairo_fill(cr);
    }
}

/*
 * Lit limb plus terminator: the right half of the disc, closed by an
 * ellipse whose x radius follows the phase.  Negative radii flip it to
 * the gibbous side, and the whole thing mirrors after full moon.
 */
static void moon_path(cairo_t *cr, double r, double phase)
{
    double a = -cos(2.0 * M_PI * phase);

    if (fabs(a) < 0.03)
	a = a < 0 ? -0.03 : 0.03;

    cairo_new_path(cr);
    cairo_arc(cr, 0, 0, r, -M_PI / 2.0, M_PI / 2.0);

    cairo_save(cr);
    cairo_scale(cr, a, 1.0);
    cairo_arc(cr, 0, 0, r, M_PI / 2.0, 3.0 * M_PI / 2.0);
    cairo_restore(cr);

    cairo_close_path(cr);
}

static void draw_moon(const AnimState *state, cairo_t *cr)
{
    double cover = state->weather.cloudcover / 100.0;
    double alpha = state->moon_alpha * (1.0 - cover * 0.85);
    double scale = state->height / 600.0;
    double r = 26.0 * scale;
    cairo_pattern_t *glow;

    if (alpha <= 0.02)
	return;

    /* Halo */
    glow = cairo_pattern_create_radial(state->moon_x, state->moon_y, r * 0.6,
				       state->moon_x, state->moon_y, r * 4.0);
    cairo_pattern_add_color_stop_rgba(glow, 0.0, 0.85, 0.90, 1.0, 0.18 * alpha);
    cairo_pattern_add_color_stop_rgba(glow, 1.0, 0.85, 0.90, 1.0, 0.0);
    cairo_set_source(cr, glow);
    cairo_arc(cr, state->moon_x, state->moon_y, r * 4.0, 0, 2.0 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(glow);

    /* Earthshine: the unlit disc, just visible */
    cairo_set_source_rgba(cr, 0.55, 0.60, 0.72, 0.16 * alpha);
    cairo_arc(cr, state->moon_x, state->moon_y, r, 0, 2.0 * M_PI);
    cairo_fill(cr);

    cairo_save(cr);
    cairo_translate(cr, state->moon_x, state->moon_y);
    if (state->moon_phase >= 0.5)
	cairo_scale(cr, -1.0, 1.0);
    moon_path(cr, r, state->moon_phase);
    cairo_restore(cr);

    cairo_set_source_rgba(cr, 0.97, 0.97, 0.90, alpha);
    cairo_fill(cr);
}

static void draw_sun(const AnimState *state, cairo_t *cr)
{
    double cover = state->weather.cloudcover / 100.0;
    double alpha = state->sun_alpha * (1.0 - cover * 0.80);
    double high = clampf(state->sun_alt * 3.0, 0.0, 1.0);
    double scale = state->height / 600.0;
    double r = lerp(34.0, 28.0, high) * scale;
    double cx = state->sun_x, cy = state->sun_y;
    cairo_pattern_t *grad;

    if (alpha <= 0.02)
	return;

    /* Low sun is deep orange and hazier, high sun is small and white */
    double cr_ = lerp(1.00, 1.00, high);
    double cg_ = lerp(0.42, 0.96, high);
    double cb_ = lerp(0.12, 0.80, high);
    double halo = lerp(5.5, 3.0, high);

    grad = cairo_pattern_create_radial(cx, cy, r * 0.5, cx, cy, r * halo);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, cr_, cg_, cb_, 0.38 * alpha);
    cairo_pattern_add_color_stop_rgba(grad, 0.4, cr_, cg_, cb_, 0.12 * alpha);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, cr_, cg_, cb_, 0.0);
    cairo_set_source(cr, grad);
    cairo_arc(cr, cx, cy, r * halo, 0, 2.0 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);

    /* Disc, brightest in the middle */
    grad = cairo_pattern_create_radial(cx, cy, 0, cx, cy, r);
    cairo_pattern_add_color_stop_rgba(grad, 0.0, 1.0, lerp(0.80, 1.0, high),
				      lerp(0.55, 0.92, high), alpha);
    cairo_pattern_add_color_stop_rgba(grad, 0.75, cr_, cg_, cb_, alpha);
    cairo_pattern_add_color_stop_rgba(grad, 1.0, cr_, cg_ * 0.85, cb_ * 0.7,
				      alpha * 0.85);
    cairo_set_source(cr, grad);
    cairo_arc(cr, cx, cy, r, 0, 2.0 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(grad);
}

static void draw_clouds(const AnimState *state, cairo_t *cr)
{
    double darkness = state->weather.cloudcover / 100.0;

    for (int i = 0; i < state->cloud_count; i++) {
        const Cloud *c = &state->clouds[i];
        double gray = lerp(0.95, 0.55, darkness);
        double alpha = c->opacity * (0.3 + darkness * 0.5);

        cairo_set_source_rgba(cr, gray, gray, gray, alpha);

        /* Draw cloud as overlapping circles */
        cairo_arc(cr, c->x, c->y, c->size * 0.6, 0, 2.0 * M_PI);
        cairo_fill(cr);
        cairo_arc(cr, c->x + c->size * 0.4, c->y - c->size * 0.15, c->size * 0.5, 0, 2.0 * M_PI);
        cairo_fill(cr);
        cairo_arc(cr, c->x - c->size * 0.35, c->y + c->size * 0.1, c->size * 0.45, 0, 2.0 * M_PI);
        cairo_fill(cr);
        cairo_arc(cr, c->x + c->size * 0.2, c->y + c->size * 0.2, c->size * 0.5, 0, 2.0 * M_PI);
        cairo_fill(cr);
    }
}

static void draw_rain(const AnimState *state, cairo_t *cr)
{
    cairo_set_source_rgba(cr, 0.6, 0.7, 0.9, 0.5);
    cairo_set_line_width(cr, 1.5);

    for (int i = 0; i < state->particle_count; i++) {
        const Particle *p = &state->particles[i];
        double len = p->size * 8.0;

        cairo_move_to(cr, p->x, p->y);
        cairo_line_to(cr, p->x - 1.0, p->y + len);
        cairo_stroke(cr);
    }
}

static void draw_snow(const AnimState *state, cairo_t *cr)
{
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);

    for (int i = 0; i < state->particle_count; i++) {
        const Particle *p = &state->particles[i];

        cairo_arc(cr, p->x, p->y, p->size, 0, 2.0 * M_PI);
        cairo_fill(cr);
    }
}

static void draw_streaks(const AnimState *state, cairo_t *cr)
{
    if (state->streak_count == 0)
        return;

    cairo_set_line_width(cr, 1.0);

    for (int i = 0; i < state->streak_count; i++) {
        const Particle *s = &state->streaks[i];
        /* Fade in/out at edges */
        double alpha = 0.12 + 0.06 * sin(state->time_accum * 1.5 + i);

        cairo_pattern_t *grad = cairo_pattern_create_linear(
            s->x - s->size, s->y, s->x, s->y);
        cairo_pattern_add_color_stop_rgba(grad, 0.0, 1.0, 1.0, 1.0, 0.0);
        cairo_pattern_add_color_stop_rgba(grad, 0.3, 1.0, 1.0, 1.0, alpha);
        cairo_pattern_add_color_stop_rgba(grad, 1.0, 1.0, 1.0, 1.0, 0.0);

        cairo_move_to(cr, s->x - s->size, s->y);
        cairo_line_to(cr, s->x, s->y);
        cairo_set_source(cr, grad);
        cairo_stroke(cr);
        cairo_pattern_destroy(grad);
    }
}

void anim_draw(const AnimState *state, cairo_t *cr)
{
    draw_sky(state, cr);
    draw_stars(state, cr);
    draw_moon(state, cr);
    draw_sun(state, cr);
    draw_clouds(state, cr);
    draw_streaks(state, cr);

    bool rain = (state->weather.type == WEATHER_RAIN ||
                 state->weather.type == WEATHER_DRIZZLE ||
                 state->weather.type == WEATHER_SHOWERS ||
                 state->weather.type == WEATHER_THUNDERSTORM);
    bool snow = (state->weather.type == WEATHER_SNOW);

    if (rain)
        draw_rain(state, cr);
    else if (snow)
        draw_snow(state, cr);
}
