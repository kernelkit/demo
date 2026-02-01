#include <math.h>
#include <stdlib.h>
#include <string.h>

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
    state->sun_ray_angle += dt * 0.5;

    update_clouds(state, dt);
    update_particles(state, dt);
    update_streaks(state, dt);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static void draw_sky(const AnimState *state, cairo_t *cr)
{
    cairo_pattern_t *grad;
    double cloud_gray = state->weather.cloudcover / 100.0;

    if (state->weather.is_day) {
        /* Daytime: blue to light blue, grayer with clouds */
        double r_top = lerp(0.15, 0.45, cloud_gray);
        double g_top = lerp(0.35, 0.45, cloud_gray);
        double b_top = lerp(0.75, 0.55, cloud_gray);

        double r_bot = lerp(0.55, 0.65, cloud_gray);
        double g_bot = lerp(0.75, 0.70, cloud_gray);
        double b_bot = lerp(0.95, 0.75, cloud_gray);

        grad = cairo_pattern_create_linear(0, 0, 0, state->height);
        cairo_pattern_add_color_stop_rgb(grad, 0.0, r_top, g_top, b_top);
        cairo_pattern_add_color_stop_rgb(grad, 1.0, r_bot, g_bot, b_bot);
    } else {
        /* Night: dark blue to very dark */
        double r_top = lerp(0.02, 0.10, cloud_gray);
        double g_top = lerp(0.02, 0.08, cloud_gray);
        double b_top = lerp(0.10, 0.12, cloud_gray);

        double r_bot = lerp(0.05, 0.12, cloud_gray);
        double g_bot = lerp(0.08, 0.10, cloud_gray);
        double b_bot = lerp(0.18, 0.15, cloud_gray);

        grad = cairo_pattern_create_linear(0, 0, 0, state->height);
        cairo_pattern_add_color_stop_rgb(grad, 0.0, r_top, g_top, b_top);
        cairo_pattern_add_color_stop_rgb(grad, 1.0, r_bot, g_bot, b_bot);
    }

    cairo_set_source(cr, grad);
    cairo_paint(cr);
    cairo_pattern_destroy(grad);
}

static void draw_sun(const AnimState *state, cairo_t *cr)
{
    if (!state->weather.is_day)
        return;
    if (state->weather.type != WEATHER_CLEAR && state->weather.type != WEATHER_PARTLY)
        return;

    double cx = state->width * 0.8;
    double cy = state->height * 0.15;
    double radius = 40.0;

    /* Rays */
    cairo_save(cr);
    cairo_translate(cr, cx, cy);

    int num_rays = 12;
    for (int i = 0; i < num_rays; i++) {
        double angle = state->sun_ray_angle + i * (2.0 * M_PI / num_rays);
        double inner = radius + 5.0;
        double outer = radius + 25.0 + sin(state->time_accum * 2.0 + i) * 8.0;

        cairo_move_to(cr, cos(angle) * inner, sin(angle) * inner);
        cairo_line_to(cr, cos(angle) * outer, sin(angle) * outer);
    }
    cairo_set_source_rgba(cr, 1.0, 0.9, 0.3, 0.6);
    cairo_set_line_width(cr, 3.0);
    cairo_stroke(cr);
    cairo_restore(cr);

    /* Sun disc with radial gradient */
    cairo_pattern_t *sun_grad = cairo_pattern_create_radial(cx, cy, 0, cx, cy, radius);
    cairo_pattern_add_color_stop_rgba(sun_grad, 0.0, 1.0, 1.0, 0.6, 1.0);
    cairo_pattern_add_color_stop_rgba(sun_grad, 0.7, 1.0, 0.85, 0.2, 0.9);
    cairo_pattern_add_color_stop_rgba(sun_grad, 1.0, 1.0, 0.7, 0.1, 0.0);

    cairo_arc(cr, cx, cy, radius, 0, 2.0 * M_PI);
    cairo_set_source(cr, sun_grad);
    cairo_fill(cr);
    cairo_pattern_destroy(sun_grad);
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
