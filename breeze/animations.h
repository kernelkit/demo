#ifndef ANIMATIONS_H
#define ANIMATIONS_H

#include <cairo.h>
#include <stdbool.h>
#include "weather.h"

#define ANIM_MAX_CLOUDS    20
#define ANIM_MAX_PARTICLES 300
#define ANIM_MAX_STREAKS   15
#define ANIM_MAX_STARS     160
#define ANIM_MAX_SPLASHES  24
#define ANIM_BOLT_POINTS   9

#define ANIM_CLOUD_PUFFS   7
#define ANIM_CLOUD_LAYERS  3

typedef struct {
    double dx, dy;         /* offset from the cloud's anchor, in size units */
    double r;              /* radius, in size units */
} Puff;

typedef struct {
    double x, y;
    double speed;
    double size;
    double opacity;
    int    layer;          /* 0 farthest, ANIM_CLOUD_LAYERS-1 nearest */
    Puff   puffs[ANIM_CLOUD_PUFFS];
    int    puff_count;
} Cloud;

typedef struct {
    double x, y;
    double speed;
    double wobble_phase;   /* snow horizontal wobble */
    double size;
} Particle;

typedef struct {
    double x, y;
    double brightness;
    double twinkle_phase;
} Star;

typedef struct {
    double x, y;
    double age;            /* 0.0 fresh, 1.0 gone */
} Splash;

typedef struct {
    /* Sky, recomputed every update from the time of day */
    double sun_alt;        /* -1 midnight, 0 horizon, +1 zenith */
    double sun_x, sun_y;
    double sun_alpha;
    double moon_x, moon_y;
    double moon_alpha;
    double moon_phase;     /* 0.0 new, 0.25 first quarter, 0.5 full */
    double horizon;        /* y of the horizon line, in pixels */

    Star   stars[ANIM_MAX_STARS];
    int    star_count;

    /* Shooting star: meteor_t counts 0..1 across one streak */
    double meteor_t;
    double next_meteor;
    double meteor_x, meteor_y;
    double meteor_dx, meteor_dy;
    double meteor_len;

    /* Clouds */
    Cloud  clouds[ANIM_MAX_CLOUDS];
    int    cloud_count;

    /* Rain / snow particles */
    Particle particles[ANIM_MAX_PARTICLES];
    int      particle_count;

    /* Wind streaks */
    Particle streaks[ANIM_MAX_STREAKS];
    int      streak_count;

    /* Wind as pixels per second across the screen, sign included */
    double   wind_vx;

    /* Rain hitting the ground */
    Splash   splashes[ANIM_MAX_SPLASHES];
    int      splash_next;

    /* Thunderstorm */
    double   flash;        /* 1.0 at the strike, decaying */
    double   next_flash;   /* seconds until the next one */
    double   bolt_x[ANIM_BOLT_POINTS];
    double   bolt_y[ANIM_BOLT_POINTS];
    int      bolt_points;

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
