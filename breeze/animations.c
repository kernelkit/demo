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
	{ 0.00,  { 0.13, 0.20, 0.44 }, { 0.52, 0.34, 0.44 }, { 0.98, 0.56, 0.30 } },
	{ 0.12,  { 0.20, 0.38, 0.70 }, { 0.55, 0.58, 0.72 }, { 0.98, 0.76, 0.52 } },
	{ 0.40,  { 0.16, 0.38, 0.78 }, { 0.35, 0.60, 0.88 }, { 0.66, 0.82, 0.94 } },
	{ 1.00,  { 0.10, 0.32, 0.76 }, { 0.30, 0.58, 0.90 }, { 0.62, 0.80, 0.96 } },
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

	/* Overcast drains the colour and the light both */
	double grey = (rgb[0] + rgb[1] + rgb[2]) / 3.0;
	double dim = lerp(1.0, 0.58, cover);

	for (int i = 0; i < 3; i++)
		rgb[i] = lerp(rgb[i], grey, cover * 0.55) * dim;
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

/* How plainly the stars are showing: nothing happens in a bright or
 * clouded sky, and the meteors keep to the same rule */
static double star_fade(const AnimState *state)
{
	double cover = state->weather.cloudcover / 100.0;

	return clampf((-state->sun_alt - 0.02) * 4.0, 0.0, 1.0) * (1.0 - cover * 0.75);
}

static void update_meteor(AnimState *state, double dt)
{
	if (star_fade(state) < 0.35) {
		state->meteor_t = 0.0;
		state->next_meteor = 10.0 + randf() * 25.0;
		return;
	}

	if (state->meteor_t > 0.0) {
		state->meteor_t += dt / 0.85;
		if (state->meteor_t >= 1.0)
			state->meteor_t = 0.0;
		return;
	}

	state->next_meteor -= dt;
	if (state->next_meteor > 0.0)
		return;

	/* Off at a shallow angle, either way across the sky */
	double ang = (22.0 + randf() * 30.0) * M_PI / 180.0;
	double dir = randf() < 0.5 ? 1.0 : -1.0;

	state->meteor_x = state->width * (dir > 0 ? randf() * 0.45 : 0.55 + randf() * 0.45);
	state->meteor_y = state->height * randf() * 0.28;
	state->meteor_dx = cos(ang) * dir;
	state->meteor_dy = sin(ang);
	state->meteor_len = state->width * (0.22 + randf() * 0.26);
	state->meteor_t = 0.0001;
	state->next_meteor = 14.0 + randf() * 40.0;
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
	set = state->weather.sunset;
	if (set - rise < 0.5) { /* no weather data yet, or polar day */
		rise = 6.0;
		set = 18.0;
	}

	state->sun_alpha = 0.0;
	state->moon_alpha = 0.0;

	if (hour >= rise && hour <= set) {
		frac = (hour - rise) / (set - rise);
		theta = M_PI * frac;
		state->sun_alt = sin(theta);
		state->sun_alpha = clampf(sin(theta) * 12.0, 0.0, 1.0);
		state->sun_x = state->width * (0.10 + 0.80 * frac);
		state->sun_y = state->horizon - sin(theta) * (state->horizon - state->height * 0.12);
	} else {
		span = 24.0 - (set - rise);
		frac = (hour > set ? hour - set : hour + 24.0 - set) / span;
		theta = M_PI * frac;
		state->sun_alt = -sin(theta);
		state->moon_alpha = clampf(sin(theta) * 8.0, 0.0, 1.0);
		state->moon_x = state->width * (0.10 + 0.80 * frac);
		state->moon_y = state->horizon - sin(theta) * (state->horizon - state->height * 0.16);
	}
}

/* ------------------------------------------------------------------ */
/* Clouds                                                             */
/* ------------------------------------------------------------------ */

/*
 * Depth comes from three layers: the far ones are small, slow, and pale,
 * the near ones big and quick.  Each cloud is a handful of overlapping
 * puffs with its own silhouette, reshuffled whenever it wraps around, so
 * the same twenty clouds never quite repeat.
 */
static const struct {
	double size, speed, opacity, top, band;
} cloud_layers[ANIM_CLOUD_LAYERS] = {
	{ 0.55, 0.45, 0.55, 0.02, 0.22 },
	{ 1.00, 1.00, 0.80, 0.06, 0.30 },
	{ 1.55, 1.75, 1.00, 0.10, 0.38 },
};

static void cloud_init(AnimState *state, Cloud *c, int layer, bool offscreen)
{
	const typeof(cloud_layers[0]) *l = &cloud_layers[layer];

	if (c->sprite) {
		cairo_surface_destroy(c->sprite);
		c->sprite = NULL;
	}

	c->layer = layer;
	c->size = (34.0 + randf() * 46.0) * l->size;
	c->speed = (7.0 + randf() * 16.0) * l->speed;
	c->opacity = (0.55 + randf() * 0.45) * l->opacity;
	c->y = state->height * (l->top + randf() * l->band);
	c->x = offscreen ? -c->size * 2.2 : randf() * state->width;

	c->puff_count = 5 + (rand() % (ANIM_CLOUD_PUFFS - 4));
	for (int i = 0; i < c->puff_count; i++) {
		Puff *p = &c->puffs[i];
		double t = (double)i / (c->puff_count - 1);

		/* Fattest in the middle, and sitting on a common base so the
		 * cloud gets a flat bottom and a domed top */
		p->dx = (t - 0.5) * 1.6;
		p->r = (0.34 + 0.26 * (1.0 - fabs(t - 0.5) * 2.0)) * (0.85 + randf() * 0.30);
		p->dy = 0.55 - p->r + (randf() - 0.5) * 0.07;
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
	for (int i = 0; i < ANIM_MAX_CLOUDS; i++)
		cloud_init(state, &state->clouds[i], i % ANIM_CLOUD_LAYERS, false);
}

/* ------------------------------------------------------------------ */
/* Update                                                             */
/* ------------------------------------------------------------------ */

static void update_clouds(AnimState *state, double dt)
{
	double cover = state->weather.cloudcover / 100.0;
	int target;

	/* A clear sky means a clear sky, not two blobs drifting through it */
	target = cover < 0.08 ? 0 : (int)(cover * ANIM_MAX_CLOUDS + 0.5);
	if (target > ANIM_MAX_CLOUDS)
		target = ANIM_MAX_CLOUDS;
	state->cloud_count = target;

	for (int i = 0; i < state->cloud_count; i++) {
		Cloud *c = &state->clouds[i];

		c->x += c->speed * dt;
		if (c->x - c->size * 1.2 > state->width)
			cloud_init(state, c, c->layer, true);
	}
}

/*
 * Wind direction is where the wind comes FROM, so a westerly (270) has
 * to push things to the right of the screen.
 */
static void update_wind(AnimState *state)
{
	double ms = state->weather.windspeed / 3.6;

	state->wind_vx = -sin(state->weather.winddirection * M_PI / 180.0) * ms * 20.0;
}

static void add_splash(AnimState *state, double x, double y)
{
	Splash *sp = &state->splashes[state->splash_next];

	sp->x = x;
	sp->y = y;
	sp->age = 0.0;
	state->splash_next = (state->splash_next + 1) % ANIM_MAX_SPLASHES;
}

static void update_splashes(AnimState *state, double dt)
{
	for (int i = 0; i < ANIM_MAX_SPLASHES; i++) {
		Splash *sp = &state->splashes[i];

		if (sp->age < 1.0)
			sp->age += dt * 2.6;
	}
}

static void strike(AnimState *state)
{
	double x = state->width * (0.15 + randf() * 0.70);
	double y = state->height * 0.14;

	state->flash = 1.0;
	state->bolt_points = ANIM_BOLT_POINTS;

	for (int i = 0; i < state->bolt_points; i++) {
		double t = (double)i / (state->bolt_points - 1);

		state->bolt_x[i] = x + (randf() - 0.5) * state->width * 0.11 * (1.0 - t * 0.4);
		state->bolt_y[i] = lerp(y, state->horizon, t);
	}
}

static void update_lightning(AnimState *state, double dt)
{
	if (state->weather.type != WEATHER_THUNDERSTORM) {
		state->flash = 0.0;
		state->next_flash = 0.0;
		return;
	}

	if (state->flash > 0.0) {
		state->flash -= dt * 3.2;
		if (state->flash < 0.0)
			state->flash = 0.0;
	}

	state->next_flash -= dt;
	if (state->next_flash <= 0.0) {
		strike(state);
		state->next_flash = 3.0 + randf() * 9.0;
	}
}

static void update_particles(AnimState *state, double dt)
{
	bool rain = (state->weather.type == WEATHER_RAIN || state->weather.type == WEATHER_DRIZZLE ||
	             state->weather.type == WEATHER_SHOWERS || state->weather.type == WEATHER_THUNDERSTORM);
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

		p->x = randf() * state->width;
		p->y = -randf() * state->height * 0.3;
		p->speed = snow ? (30.0 + randf() * 40.0) : (200.0 + randf() * 300.0);
		p->wobble_phase = randf() * M_PI * 2.0;
		p->size = snow ? (2.0 + randf() * 3.0) : (1.0 + randf() * 1.5);
		state->particle_count++;
	}

	/* Remove excess */
	if (state->particle_count > target)
		state->particle_count = target;

	for (int i = 0; i < state->particle_count; i++) {
		Particle *p = &state->particles[i];

		p->y += p->speed * dt;
		p->x += state->wind_vx * (snow ? 0.45 : 1.0) * dt;

		if (snow) {
			p->wobble_phase += dt * 2.0;
			p->x += sin(p->wobble_phase) * 20.0 * dt;
		}

		/* Blown off the side, come back on the other one */
		if (p->x < -20.0)
			p->x += state->width + 40.0;
		else if (p->x > state->width + 20.0)
			p->x -= state->width + 40.0;

		/* Land on the ground rather than sail past it */
		if (p->y > state->horizon) {
			if (!snow && randf() < 0.5)
				add_splash(state, p->x, state->horizon);
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

		if (frac > 1.0)
			frac = 1.0;
		target = (int)(frac * ANIM_MAX_STREAKS);
		if (target < 1)
			target = 1;
	}

	while (state->streak_count < target) {
		Particle *s = &state->streaks[state->streak_count];

		s->x = state->wind_vx < 0 ? state->width * (1.0 + randf() * 0.3) :
		                            -randf() * state->width * 0.3;
		s->y = randf() * state->height;
		s->speed = 150.0 + wind_ms * 20.0 + randf() * 100.0;
		s->size = 30.0 + randf() * 50.0; /* streak length */
		state->streak_count++;
	}

	if (state->streak_count > target)
		state->streak_count = target;

	for (int i = 0; i < state->streak_count; i++) {
		Particle *s = &state->streaks[i];
		double dir = state->wind_vx < 0 ? -1.0 : 1.0;

		s->x += s->speed * dir * dt;

		if ((dir > 0 && s->x > state->width + s->size) || (dir < 0 && s->x < -s->size)) {
			s->x = dir > 0 ? -s->size - randf() * state->width * 0.2 :
			                 state->width + s->size + randf() * state->width * 0.2;
			s->y = randf() * state->height * 0.85;
			s->speed = 150.0 + wind_ms * 20.0 + randf() * 100.0;
		}
	}
}

void anim_update(AnimState *state, double dt, const WeatherData *weather)
{
	state->weather = *weather;
	state->time_accum += dt;

	update_sky(state);
	update_meteor(state, dt);
	update_wind(state);
	update_clouds(state, dt);
	update_particles(state, dt);
	update_streaks(state, dt);
	update_splashes(state, dt);
	update_lightning(state, dt);
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
	double fade = star_fade(state);

	if (fade <= 0.01)
		return;

		/* One fill per brightness bucket rather than one per star: the
		 * twinkle survives, the couple of hundred draw calls do not */
#define STAR_BUCKETS 5
	for (int b = 0; b < STAR_BUCKETS; b++) {
		double lo = (double)b / STAR_BUCKETS;
		double hi = (double)(b + 1) / STAR_BUCKETS;
		bool any = false;

		cairo_new_path(cr);
		for (int i = 0; i < state->star_count; i++) {
			const Star *st = &state->stars[i];
			double tw = 0.65 + 0.35 * sin(state->time_accum * 1.7 + st->twinkle_phase);
			double a = st->brightness * tw;

			if (a < lo || a >= hi)
				continue;

			cairo_new_sub_path(cr);
			cairo_arc(cr, st->x, st->y, st->brightness * 1.3, 0, 2.0 * M_PI);
			any = true;
		}

		if (!any)
			continue;

		cairo_set_source_rgba(cr, 1.0, 0.98, 0.92, (lo + hi) / 2.0 * fade);
		cairo_fill(cr);
	}
}

static void draw_meteor(const AnimState *state, cairo_t *cr)
{
	double p = state->meteor_t;
	double travel, tail, hx, hy, tx, ty, a;
	cairo_pattern_t *g;

	if (p <= 0.0)
		return;

	travel = state->meteor_len * p;
	tail = travel < state->meteor_len * 0.38 ? travel : state->meteor_len * 0.38;
	hx = state->meteor_x + state->meteor_dx * travel;
	hy = state->meteor_y + state->meteor_dy * travel;
	tx = hx - state->meteor_dx * tail;
	ty = hy - state->meteor_dy * tail;
	a = sin(M_PI * p) * star_fade(state);

	g = cairo_pattern_create_linear(tx, ty, hx, hy);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.75, 0.85, 1.0, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 1.0, 0.98, 0.92, a);
	cairo_set_source(cr, g);
	cairo_set_line_width(cr, 2.0);
	cairo_move_to(cr, tx, ty);
	cairo_line_to(cr, hx, hy);
	cairo_stroke(cr);
	cairo_pattern_destroy(g);

	cairo_set_source_rgba(cr, 1.0, 1.0, 0.95, a);
	cairo_arc(cr, hx, hy, 1.6, 0, 2.0 * M_PI);
	cairo_fill(cr);
}

/*
 * Near the horizon refraction lifts the lower limb of a disc further
 * than the upper one, squashing it into an ellipse -- roughly a sixth
 * shorter as it touches down.  It is the same effect the -35/60 degree
 * constant in sunriset.h accounts for when deciding what counts as
 * sunrise.  Unlike the swelling below, this one is real.
 */
static double horizon_flatten(double alt)
{
	return lerp(0.85, 1.0, clampf(alt * 6.0, 0.0, 1.0));
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
	/* At night sun_alt carries the moon's arc, negated */
	double high = clampf(-state->sun_alt * 3.0, 0.0, 1.0);
	/*
	 * The moon illusion is perceptual, so it cannot happen on a panel
	 * a metre from your face.  Draw it in instead: a disc of constant
	 * size reads as wrong to anyone who has watched a moon come up.
	 */
	double r = lerp(31.5, 26.0, high) * scale;
	double flat = horizon_flatten(-state->sun_alt);
	double halo = lerp(5.0, 4.0, high);
	cairo_pattern_t *glow;

	if (alpha <= 0.02)
		return;

	/* Halo, left round: the glow is scattering, not the disc */
	glow = cairo_pattern_create_radial(state->moon_x, state->moon_y, r * 0.6, state->moon_x,
	                                   state->moon_y, r * halo);
	cairo_pattern_add_color_stop_rgba(glow, 0.0, 0.85, 0.90, 1.0, 0.18 * alpha);
	cairo_pattern_add_color_stop_rgba(glow, 1.0, 0.85, 0.90, 1.0, 0.0);
	cairo_set_source(cr, glow);
	cairo_arc(cr, state->moon_x, state->moon_y, r * halo, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(glow);

	/* Earthshine: the unlit disc, just visible */
	cairo_save(cr);
	cairo_translate(cr, state->moon_x, state->moon_y);
	cairo_scale(cr, 1.0, flat);
	cairo_arc(cr, 0, 0, r, 0, 2.0 * M_PI);
	cairo_restore(cr);
	cairo_set_source_rgba(cr, 0.55, 0.60, 0.72, 0.16 * alpha);
	cairo_fill(cr);

	cairo_save(cr);
	cairo_translate(cr, state->moon_x, state->moon_y);
	cairo_scale(cr, 1.0, flat);
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
	/* Swollen near the horizon, standing in for the sun illusion */
	double r = lerp(34.0, 28.0, high) * scale;
	double flat = horizon_flatten(state->sun_alt);
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

	/* Disc, squashed by refraction when it is low */
	cairo_save(cr);
	cairo_translate(cr, cx, cy);
	cairo_scale(cr, 1.0, flat);
	grad = cairo_pattern_create_radial(0, 0, 0, 0, 0, r);
	cairo_pattern_add_color_stop_rgba(grad, 0.0, 1.0, lerp(0.80, 1.0, high), lerp(0.55, 0.92, high),
	                                  alpha);
	cairo_pattern_add_color_stop_rgba(grad, 0.75, cr_, cg_, cb_, alpha);
	cairo_pattern_add_color_stop_rgba(grad, 1.0, cr_, cg_ * 0.85, cb_ * 0.7, alpha * 0.85);
	cairo_set_source(cr, grad);
	cairo_arc(cr, 0, 0, r, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_pattern_destroy(grad);
	cairo_restore(cr);
}

/*
 * The light changes slowly, the clouds move every frame.  So each cloud
 * is painted once into its own sprite and then just blitted while it
 * drifts, which keeps a few hundred gradient fills per frame off the X
 * server.  light_key says when the painting is stale.
 */
static int light_key(const AnimState *state)
{
	return (int)(state->sun_alt * 60.0) * 1000 + state->weather.cloudcover * 4 + (int)state->weather.type;
}

static void cloud_colours(const AnimState *state, const Cloud *c, double lit[3], double shade[3])
{
	double cover = state->weather.cloudcover / 100.0;
	double day = clampf(state->sun_alt * 4.0 + 0.35, 0.0, 1.0);
	double warm = clampf(1.0 - fabs(state->sun_alt) * 5.0, 0.0, 1.0);

	(void)c;

	lit[0] = lerp(0.20, 1.00, day);
	lit[1] = lerp(0.22, 1.00, day);
	lit[2] = lerp(0.30, 1.00, day);
	shade[0] = lerp(0.12, 0.62, day);
	shade[1] = lerp(0.13, 0.64, day);
	shade[2] = lerp(0.20, 0.72, day);

	for (int k = 0; k < 3; k++) {
		double grey = lerp(1.0, 0.50, cover);

		lit[k] *= grey;
		shade[k] *= grey;
	}

	/* Golden hour warms the lit side and reddens the shadow */
	lit[0] = lerp(lit[0], lit[0] * 1.08 + 0.10, warm);
	lit[1] = lerp(lit[1], lit[1] * 0.92 + 0.02, warm);
	lit[2] = lerp(lit[2], lit[2] * 0.74, warm);
	shade[0] = lerp(shade[0], shade[0] * 1.15 + 0.10, warm);
	shade[2] = lerp(shade[2], shade[2] * 0.90, warm);
}

static void render_cloud(AnimState *state, Cloud *c, cairo_t *target)
{
	double lit[3], shade[3];
	double dirx = 0.0, diry = -1.0, ext, len, lx, ly;
	cairo_t *cr;
	cairo_pattern_t *g;

	if (c->sprite)
		cairo_surface_destroy(c->sprite);

	c->sprite_hw = c->size * 1.85;
	c->sprite_hh = c->size * 1.35;
	c->sprite = cairo_surface_create_similar(cairo_get_target(target), CAIRO_CONTENT_COLOR_ALPHA,
	                                         (int)(c->sprite_hw * 2.0), (int)(c->sprite_hh * 2.0));
	c->sprite_key = light_key(state);

	cloud_colours(state, c, lit, shade);

	/* Light direction is baked in at the cloud's present position */
	if (state->sun_alpha > state->moon_alpha) {
		lx = state->sun_x;
		ly = state->sun_y;
	} else {
		lx = state->moon_x;
		ly = state->moon_y;
	}

	cr = cairo_create(c->sprite);
	cairo_translate(cr, c->sprite_hw, c->sprite_hh);

	/* Haze first, so the rim dissolves instead of ending */
	for (int j = 0; j < c->puff_count; j++) {
		const Puff *pf = &c->puffs[j];
		double px = pf->dx * c->size;
		double py = pf->dy * c->size;
		double pr = pf->r * c->size * 1.38;
		double mid[3];

		for (int k = 0; k < 3; k++)
			mid[k] = (lit[k] + shade[k]) / 2.0;

		g = cairo_pattern_create_radial(px, py, pr * 0.30, px, py, pr);
		cairo_pattern_add_color_stop_rgba(g, 0.0, mid[0], mid[1], mid[2], c->opacity * 0.62);
		cairo_pattern_add_color_stop_rgba(g, 0.55, mid[0], mid[1], mid[2], c->opacity * 0.38);
		cairo_pattern_add_color_stop_rgba(g, 1.0, mid[0], mid[1], mid[2], 0.0);
		cairo_set_source(cr, g);
		cairo_arc(cr, px, py, pr, 0, 2.0 * M_PI);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}

	/* Then the body as one path, so the puffs stop reading as separate
	 * balls, lit from above across the whole cloud */
	cairo_new_path(cr);
	for (int j = 0; j < c->puff_count; j++) {
		const Puff *pf = &c->puffs[j];

		cairo_new_sub_path(cr);
		cairo_arc(cr, pf->dx * c->size, pf->dy * c->size, pf->r * c->size * 0.82, 0, 2.0 * M_PI);
	}

	/* Shade along the axis to the light, not merely downwards, so a low
	 * sun lights the clouds from the side */
	len = hypot(lx - c->x, ly - c->y);
	if (len > 1.0) {
		dirx = (lx - c->x) / len;
		diry = (ly - c->y) / len;
	}
	ext = c->size * 0.95;

	g = cairo_pattern_create_linear(dirx * ext, diry * ext, -dirx * ext, -diry * ext);
	cairo_pattern_add_color_stop_rgba(g, 0.0, lit[0], lit[1], lit[2], c->opacity);
	cairo_pattern_add_color_stop_rgba(g, 0.55, lerp(lit[0], shade[0], 0.55), lerp(lit[1], shade[1], 0.55),
	                                  lerp(lit[2], shade[2], 0.55), c->opacity);
	cairo_pattern_add_color_stop_rgba(g, 1.0, shade[0], shade[1], shade[2], c->opacity * 0.94);
	cairo_set_source(cr, g);
	cairo_fill(cr);
	cairo_pattern_destroy(g);

	cairo_destroy(cr);
}

static void draw_clouds(AnimState *state, cairo_t *cr)
{
	int key = light_key(state);

	for (int layer = 0; layer < ANIM_CLOUD_LAYERS; layer++) {
		for (int i = 0; i < state->cloud_count; i++) {
			Cloud *c = &state->clouds[i];

			if (c->layer != layer)
				continue;

			if (!c->sprite || c->sprite_key != key)
				render_cloud(state, c, cr);

			cairo_set_source_surface(cr, c->sprite, c->x - c->sprite_hw, c->y - c->sprite_hh);
			cairo_paint(cr);
		}
	}
}

/* Two ridges of hills, so the sky has somewhere to land */
static void hill_path(const AnimState *state, cairo_t *cr, double base, double amp, double freq, double phase)
{
	cairo_new_path(cr);
	cairo_move_to(cr, 0, state->height);

	for (double x = 0; x <= state->width; x += 6.0) {
		double y = base -
		           amp * (0.62 * sin(x * freq + phase) + 0.38 * sin(x * freq * 2.7 + phase * 1.9));
		cairo_line_to(cr, x, y);
	}

	cairo_line_to(cr, state->width, state->height);
	cairo_close_path(cr);
}

static void near_ridge(const AnimState *state, cairo_t *cr)
{
	hill_path(state, cr, state->horizon + state->height * 0.055, state->height * 0.038,
	          2.1 / state->width * M_PI, 2.6);
}

static void draw_horizon(const AnimState *state, cairo_t *cr)
{
	double bot[3];

	sky_stop(state, 2, bot);

	/* Far ridge, hazy: the sky's own colour, knocked well down */
	hill_path(state, cr, state->horizon, state->height * 0.045, 3.4 / state->width * M_PI, 0.8);
	cairo_set_source_rgb(cr, bot[0] * 0.42, bot[1] * 0.40, bot[2] * 0.46);
	cairo_fill(cr);

	/* Near ridge, near enough black */
	near_ridge(state, cr);
	cairo_set_source_rgb(cr, bot[0] * 0.16, bot[1] * 0.15, bot[2] * 0.20);
	cairo_fill(cr);
}

/* Snow settles along the near ridge instead of vanishing into it */
static void draw_snow_cap(const AnimState *state, cairo_t *cr)
{
	if (state->weather.type != WEATHER_SNOW)
		return;

	near_ridge(state, cr);
	cairo_set_source_rgba(cr, 0.95, 0.96, 1.0, 0.55);
	cairo_set_line_width(cr, 5.0);
	cairo_stroke(cr);
}

static void draw_vignette(const AnimState *state, cairo_t *cr)
{
	double cx = state->width / 2.0;
	double cy = state->height / 2.0;
	double r = hypot(cx, cy);
	cairo_pattern_t *g;

	g = cairo_pattern_create_radial(cx, cy, r * 0.45, cx, cy, r);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0, 0, 0, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0, 0, 0, 0.30);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);
}

static void draw_rain(const AnimState *state, cairo_t *cr)
{
	cairo_set_source_rgba(cr, 0.6, 0.7, 0.9, 0.5);
	cairo_set_line_width(cr, 1.5);

	cairo_new_path(cr);
	for (int i = 0; i < state->particle_count; i++) {
		const Particle *p = &state->particles[i];
		double len = p->size * 8.0 + p->speed * 0.02;
		double norm = hypot(state->wind_vx, p->speed);

		/* Drops lie along the way they are actually travelling */
		cairo_move_to(cr, p->x, p->y);
		cairo_line_to(cr, p->x + state->wind_vx / norm * len, p->y + p->speed / norm * len);
	}
	cairo_stroke(cr);
}

static void draw_splashes(const AnimState *state, cairo_t *cr)
{
	cairo_set_line_width(cr, 1.2);

	for (int i = 0; i < ANIM_MAX_SPLASHES; i++) {
		const Splash *sp = &state->splashes[i];
		double r, a;

		if (sp->age >= 1.0)
			continue;

		r = 2.0 + sp->age * 11.0;
		a = (1.0 - sp->age) * 0.45;

		cairo_set_source_rgba(cr, 0.72, 0.80, 0.95, a);
		cairo_save(cr);
		cairo_translate(cr, sp->x, sp->y);
		cairo_scale(cr, 1.0, 0.40);
		cairo_arc(cr, 0, 0, r, M_PI, 2.0 * M_PI);
		cairo_restore(cr);
		cairo_stroke(cr);
	}
}

static void draw_fog(const AnimState *state, cairo_t *cr)
{
	cairo_pattern_t *g;

	if (state->weather.type != WEATHER_FOG)
		return;

	/* A veil thickening towards the ground */
	g = cairo_pattern_create_linear(0, state->height * 0.30, 0, state->horizon);
	cairo_pattern_add_color_stop_rgba(g, 0.0, 0.80, 0.82, 0.85, 0.0);
	cairo_pattern_add_color_stop_rgba(g, 1.0, 0.82, 0.84, 0.87, 0.55);
	cairo_set_source(cr, g);
	cairo_paint(cr);
	cairo_pattern_destroy(g);

	/* Banks drifting through it */
	for (int i = 0; i < 6; i++) {
		double y = state->horizon - (i + 0.4) * state->height * 0.075 +
		           sin(state->time_accum * 0.13 + i * 1.7) * 7.0;
		double h = state->height * 0.055;
		double a = 0.10 + 0.06 * sin(state->time_accum * 0.21 + i);
		double off =
		        fmod(state->time_accum * (6.0 + i * 2.0) + state->wind_vx * 0.1 * state->time_accum,
		             400.0);

		g = cairo_pattern_create_linear(0, y - h / 2, 0, y + h / 2);
		cairo_pattern_add_color_stop_rgba(g, 0.0, 0.88, 0.90, 0.93, 0.0);
		cairo_pattern_add_color_stop_rgba(g, 0.5, 0.88, 0.90, 0.93, a);
		cairo_pattern_add_color_stop_rgba(g, 1.0, 0.88, 0.90, 0.93, 0.0);
		cairo_set_source(cr, g);
		cairo_rectangle(cr, -200 + fmod(off, 60.0), y - h / 2, state->width + 400, h);
		cairo_fill(cr);
		cairo_pattern_destroy(g);
	}
}

static void draw_lightning(const AnimState *state, cairo_t *cr)
{
	double f = state->flash;

	if (f <= 0.0)
		return;

	/* The bolt is only there for the brightest part of the flash */
	if (f > 0.45 && state->bolt_points > 1) {
		cairo_move_to(cr, state->bolt_x[0], state->bolt_y[0]);
		for (int i = 1; i < state->bolt_points; i++)
			cairo_line_to(cr, state->bolt_x[i], state->bolt_y[i]);

		cairo_set_source_rgba(cr, 0.85, 0.90, 1.0, 0.35 * f);
		cairo_set_line_width(cr, 9.0);
		cairo_stroke_preserve(cr);

		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, f);
		cairo_set_line_width(cr, 2.2);
		cairo_stroke(cr);
	}

	cairo_set_source_rgba(cr, 1.0, 1.0, 0.97, 0.55 * f * f);
	cairo_paint(cr);
}

static void draw_snow(const AnimState *state, cairo_t *cr)
{
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);

	cairo_new_path(cr);
	for (int i = 0; i < state->particle_count; i++) {
		const Particle *p = &state->particles[i];

		cairo_new_sub_path(cr);
		cairo_arc(cr, p->x, p->y, p->size, 0, 2.0 * M_PI);
	}
	cairo_fill(cr);
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

		cairo_pattern_t *grad = cairo_pattern_create_linear(s->x - s->size, s->y, s->x, s->y);
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

/*
 * Sky, hills, and vignette are full-screen gradients that the X server
 * was re-evaluating thirty times a second to produce the same pixels.
 * Keep them in surfaces and repaint only when the light has moved on.
 */
static void ensure_layers(AnimState *state, cairo_t *target)
{
	int key = light_key(state);
	cairo_surface_t *surf = cairo_get_target(target);
	cairo_t *cr;

	if (state->sky_layer && state->layer_key == key && state->layer_w == state->width &&
	    state->layer_h == state->height)
		return;

	if (state->sky_layer)
		cairo_surface_destroy(state->sky_layer);
	if (state->hill_layer)
		cairo_surface_destroy(state->hill_layer);

	state->sky_layer =
	        cairo_surface_create_similar(surf, CAIRO_CONTENT_COLOR, state->width, state->height);
	cr = cairo_create(state->sky_layer);
	draw_sky(state, cr);
	cairo_destroy(cr);

	state->hill_layer =
	        cairo_surface_create_similar(surf, CAIRO_CONTENT_COLOR_ALPHA, state->width, state->height);
	cr = cairo_create(state->hill_layer);
	draw_horizon(state, cr);
	cairo_destroy(cr);

	/* The vignette never changes but for the size */
	if (state->layer_w != state->width || state->layer_h != state->height || !state->vignette_layer) {
		if (state->vignette_layer)
			cairo_surface_destroy(state->vignette_layer);
		state->vignette_layer = cairo_surface_create_similar(surf, CAIRO_CONTENT_COLOR_ALPHA,
		                                                     state->width, state->height);
		cr = cairo_create(state->vignette_layer);
		draw_vignette(state, cr);
		cairo_destroy(cr);
	}

	state->layer_key = key;
	state->layer_w = state->width;
	state->layer_h = state->height;
}

static void blit(cairo_t *cr, cairo_surface_t *surf)
{
	cairo_set_source_surface(cr, surf, 0, 0);
	cairo_paint(cr);
}

void anim_draw(AnimState *state, cairo_t *cr)
{
	ensure_layers(state, cr);

	blit(cr, state->sky_layer);
	draw_stars(state, cr);
	draw_meteor(state, cr);
	draw_moon(state, cr);
	draw_sun(state, cr);
	draw_clouds(state, cr);
	blit(cr, state->hill_layer);
	draw_snow_cap(state, cr);
	draw_streaks(state, cr);

	bool rain = (state->weather.type == WEATHER_RAIN || state->weather.type == WEATHER_DRIZZLE ||
	             state->weather.type == WEATHER_SHOWERS || state->weather.type == WEATHER_THUNDERSTORM);
	bool snow = (state->weather.type == WEATHER_SNOW);

	if (rain) {
		draw_rain(state, cr);
		draw_splashes(state, cr);
	} else if (snow) {
		draw_snow(state, cr);
	}

	draw_fog(state, cr);
	draw_lightning(state, cr);
	blit(cr, state->vignette_layer);
}
