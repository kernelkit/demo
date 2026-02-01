#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include <cairo.h>
#include <stdbool.h>
#include "weather.h"

#define ANIM_MAX_CLOUDS    20
#define ANIM_MAX_PARTICLES 300

typedef struct {
    double x, y;
    double speed;
    double size;
    double opacity;
} Cloud;

typedef struct {
    double x, y;
    double speed;
    double wobble_phase;   /* snow horizontal wobble */
    double size;
} Particle;

typedef struct {
    /* Sun */
    double sun_ray_angle;

    /* Clouds */
    Cloud  clouds[ANIM_MAX_CLOUDS];
    int    cloud_count;

    /* Rain / snow particles */
    Particle particles[ANIM_MAX_PARTICLES];
    int      particle_count;

    /* Screen dimensions */
    int width;
    int height;

    /* Current weather state driving the animation */
    WeatherData weather;
    double      time_accum;
} AnimState;

/* Initialize animation state for given screen dimensions */
void anim_init(AnimState *state, int width, int height);

/* Update animation by dt seconds using current weather data */
void anim_update(AnimState *state, double dt, const WeatherData *weather);

/* Draw all animation layers to the Cairo context */
void anim_draw(const AnimState *state, cairo_t *cr);

#endif /* ANIMATIONS_H */
