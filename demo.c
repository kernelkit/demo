/*
 * Infix Demo — Classic demoscene-style effects
 * Copyright (c) 2025  Joachim Wiberg <troglobit@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>

/* Embedded font, image, and music data */
#include "font_data.h"
#include "image_data.h"
#include "logo_data.h"

/* Music data will be included when available */
#ifdef HAVE_MUSIC
#include "music_data.h"
#endif

/* Base dimensions - can be overridden at runtime */
static int WIDTH = 800;
static int HEIGHT = 600;

#define PI 3.14159265358979323846
#define NUM_STARS 200
#define MAX_LOGO_PARTICLES 8192

typedef enum {
    SCROLL_NONE,
    SCROLL_SINE_WAVE,
    SCROLL_CLASSIC,
    SCROLL_ROLLER_3D,
    SCROLL_BOUNCE
} ScrollStyle;

typedef struct {
    float x, y, z;
} Star;

typedef struct {
    float x, y;          /* Current position */
    float vx, vy;        /* Velocity */
    Uint32 color;        /* Pixel color from logo */
    int active;          /* Is this particle alive? */
    float wobble_phase;  /* For wobble animation */
} LogoParticle;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *plasma_texture;
    Uint32 *pixels;
    TTF_Font *font;
    TTF_Font *font_outline;
    SDL_Surface *jack_surface;
    SDL_Texture *jack_texture;
    SDL_Surface *logo_surface;
    SDL_Texture *logo_texture;
    int current_scene;
    int current_scene_index;  /* Index into scene_list */
    int fixed_scene;
    float time;
    float global_time;
    float fade_alpha;
    int fading;
    ScrollStyle scroll_style;
    Star stars[NUM_STARS];
    Uint32 scene_duration;  /* Milliseconds per scene */
    int scene_list[16];     /* Custom scene order */
    int num_scenes;         /* Number of scenes in list */
    char *scroll_text;      /* Dynamically loaded scroll text */
    /* Scroll control state */
    float scroll_speed;     /* Current scroll speed */
    float scroll_pause_until; /* Global time to pause until */
    Uint8 scroll_color[3];  /* Current scroll color (RGB) */
    float scroll_offset;    /* Accumulated scroll offset */
    float last_frame_time;  /* Time of last frame for delta calculation */
    int roller_effect;      /* Roller text effect: 0=all, 1=no outline, 2=no outline/glow, 3=color outline */
} DemoContext;

/* Plasma effect - optimized with lower resolution and LUT */
void render_plasma(DemoContext *ctx)
{
	#define PLASMA_W 400
	#define PLASMA_H 300

	/* Use global_time so plasma doesn't reset every scene */
	float t = ctx->global_time * 0.8;

	/* Add drift to the plasma with slow-moving offsets */
	float drift_x = sin(ctx->global_time * 0.15) * 50.0;
	float drift_y = cos(ctx->global_time * 0.2) * 40.0;

	/* Precompute sine LUTs for this frame */
	static float sinx[PLASMA_W * 2];
	static float siny[PLASMA_H * 2];

	for (int i = 0; i < PLASMA_W * 2; i++)
		sinx[i] = sin(i * 0.02 + t);
	for (int j = 0; j < PLASMA_H * 2; j++)
		siny[j] = sin(j * 0.02 + t);

	/* Lock plasma texture for direct pixel access */
	Uint32 *pixels;
	int pitch;
	SDL_LockTexture(ctx->plasma_texture, NULL, (void**)&pixels, &pitch);
	int stride = pitch / 4;

	for (int y = 0; y < PLASMA_H; y++) {
		for (int x = 0; x < PLASMA_W; x++) {
			int fx = (int)(x + drift_x);
			int fy = (int)(y + drift_y);

			/* Clamp to LUT bounds */
			fx = (fx < 0) ? 0 : ((fx >= PLASMA_W * 2) ? PLASMA_W * 2 - 1 : fx);
			fy = (fy < 0) ? 0 : ((fy >= PLASMA_H * 2) ? PLASMA_H * 2 - 1 : fy);

			/* Use LUT and radial term with proper distance */
			int dx = x - PLASMA_W / 2;
			int dy = y - PLASMA_H / 2;
			float dist = sqrtf(dx * dx + dy * dy);

			float v = sinx[fx] + siny[fy] +
			         sinx[(fx + fy) % (PLASMA_W * 2)] +
			         sin(dist * 0.02 + t * 1.2);

			/* Direct color calculation (simpler and more accurate) */
			int r = (int)(128 + 127 * sin(v * PI));
			int g = (int)(128 + 127 * sin(v * PI + 2 * PI / 3));
			int b = (int)(128 + 127 * sin(v * PI + 4 * PI / 3));

			pixels[y * stride + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
		}
	}

	SDL_UnlockTexture(ctx->plasma_texture);
}

/* Starfield effect */
void render_starfield(DemoContext *ctx)
{
	/* Clear to black */
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		ctx->pixels[i] = 0xFF000000;
	}

	/* Update and render stars */
	float speed = 100.0f;
	for (int i = 0; i < NUM_STARS; i++) {
		/* Move star towards camera */
		ctx->stars[i].z -= speed * 0.016f;

		/* Reset star if it passes the camera */
		if (ctx->stars[i].z <= 0) {
			ctx->stars[i].x = (rand() % 2000 - 1000) / 10.0f;
			ctx->stars[i].y = (rand() % 2000 - 1000) / 10.0f;
			ctx->stars[i].z = 100.0f;
		}

		/* Project 3D to 2D */
		float k = 128.0f / ctx->stars[i].z;
		int sx = WIDTH / 2 + (int)(ctx->stars[i].x * k);
		int sy = HEIGHT / 2 + (int)(ctx->stars[i].y * k);

		/* Calculate brightness based on distance */
		int brightness = (int)(255 * (1.0f - ctx->stars[i].z / 100.0f));
		if (brightness < 0) brightness = 0;
		if (brightness > 255) brightness = 255;

		/* Draw star */
		if (sx >= 0 && sx < WIDTH && sy >= 0 && sy < HEIGHT) {
			Uint32 color = 0xFF000000 | (brightness << 16) | (brightness << 8) | brightness;
			ctx->pixels[sy * WIDTH + sx] = color;

			/* Draw larger stars for closer ones */
			if (ctx->stars[i].z < 20.0f && sx > 0 && sy > 0 && sx < WIDTH - 1 && sy < HEIGHT - 1) {
				ctx->pixels[sy * WIDTH + sx - 1] = color;
				ctx->pixels[sy * WIDTH + sx + 1] = color;
				ctx->pixels[(sy - 1) * WIDTH + sx] = color;
				ctx->pixels[(sy + 1) * WIDTH + sx] = color;
			}
		}
	}

	/* Update texture */
	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);
}

/* Text scroller with SDL_ttf */
void render_scroller(DemoContext *ctx)
{
	/* Clear to dark background */
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		ctx->pixels[i] = 0xFF000020;
	}

	/* Update texture first so we can render text on top */
	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);

	/* Just set the scroll style - the actual rendering is done by render_scroll_text */
	ctx->scroll_style = SCROLL_SINE_WAVE;
}

/* Get pixel from jack surface with bounds checking */
Uint32 get_jack_pixel(SDL_Surface *surface, int x, int y)
{
	if (!surface || x < 0 || y < 0 || x >= surface->w || y >= surface->h) {
		return 0xFF000000;
	}
	Uint32 *pixels = (Uint32 *)surface->pixels;
	return pixels[y * surface->w + x];
}

/* Rotating cube with texture mapped faces and copper bars */
void render_cube(DemoContext *ctx)
{
	/* Clear to black first */
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		ctx->pixels[i] = 0xFF000000;
	}

	/* Render copper bars */
	float t = ctx->time;
	int num_bars = 8;
	for (int i = 0; i < num_bars; i++) {
		/* Calculate bar position with sine wave motion */
		float base_y = (i * HEIGHT / num_bars) + sin(t * 1.5 + i * 0.8) * 40.0;
		int bar_height = 30;

		/* HSV to RGB for rainbow effect */
		float hue = (i / (float)num_bars + t * 0.1);
		hue = hue - floor(hue);  /* Keep in 0-1 range */

		int h_section = (int)(hue * 6);
		float f = hue * 6 - h_section;
		int v = 255;
		int p = 0;
		int q = (int)(v * (1 - f));
		int t_val = (int)(v * f);

		int r, g, b;
		switch (h_section % 6) {
		case 0: r = v; g = t_val; b = p; break;
		case 1: r = q; g = v; b = p; break;
		case 2: r = p; g = v; b = t_val; break;
		case 3: r = p; g = q; b = v; break;
		case 4: r = t_val; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
		}

		/* Draw bar with gradient */
		for (int dy = 0; dy < bar_height; dy++) {
			int y = (int)base_y + dy;
			if (y >= 0 && y < HEIGHT) {
				/* Gradient brightness based on position in bar */
				float brightness = 1.0f - fabsf(dy - bar_height / 2.0f) / (bar_height / 2.0f);
				brightness = brightness * brightness;  /* Squared for sharper falloff */

				int br = (int)(r * brightness);
				int bg = (int)(g * brightness);
				int bb = (int)(b * brightness);

				Uint32 color = 0xFF000000 | (br << 16) | (bg << 8) | bb;
				for (int x = 0; x < WIDTH; x++) {
					ctx->pixels[y * WIDTH + x] = color;
				}
			}
		}
	}

	if (!ctx->jack_surface) {
		SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
		SDL_RenderClear(ctx->renderer);
		SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);
		return;
	}

	/* Define cube vertices */
	float vertices[8][3] = {
		{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
		{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}
	};

	/* Rotate and project */
	float angle_x = ctx->time * 0.7;
	float angle_y = ctx->time * 0.5;
	float angle_z = ctx->time * 0.3;

	float rotated[8][3];
	int projected[8][2];

	for (int i = 0; i < 8; i++) {
		float x = vertices[i][0];
		float y = vertices[i][1];
		float z = vertices[i][2];

		/* Rotate around X */
		float y1 = y * cos(angle_x) - z * sin(angle_x);
		float z1 = y * sin(angle_x) + z * cos(angle_x);
		y = y1;
		z = z1;

		/* Rotate around Y */
		float x1 = x * cos(angle_y) + z * sin(angle_y);
		z1 = -x * sin(angle_y) + z * cos(angle_y);
		x = x1;
		z = z1;

		/* Rotate around Z */
		float x2 = x * cos(angle_z) - y * sin(angle_z);
		float y2 = x * sin(angle_z) + y * cos(angle_z);
		x = x2;
		y = y2;

		rotated[i][0] = x;
		rotated[i][1] = y;
		rotated[i][2] = z;

		/* Simple perspective projection */
		float scale = 150.0 / (4.0 + z);
		projected[i][0] = WIDTH / 2 + (int)(x * scale);
		projected[i][1] = HEIGHT / 2 + (int)(y * scale);
	}

	/* Update texture and render background first */
	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);

	/* Define faces */
	int faces[6][4] = {
		{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
		{2, 3, 7, 6}, {0, 3, 7, 4}, {1, 2, 6, 5}
	};

	/* Build sortable face list */
	struct facez {
		int idx;
		float z;
	} fl[6];

	for (int f = 0; f < 6; f++) {
		float avg_z = (rotated[faces[f][0]][2] + rotated[faces[f][1]][2] +
		               rotated[faces[f][2]][2] + rotated[faces[f][3]][2]) / 4.0f;
		fl[f].idx = f;
		fl[f].z = avg_z;
	}

	/* Sort back-to-front (furthest first) */
	for (int i = 0; i < 5; i++) {
		for (int j = i + 1; j < 6; j++) {
			if (fl[i].z < fl[j].z) {
				struct facez tmp = fl[i];
				fl[i] = fl[j];
				fl[j] = tmp;
			}
		}
	}

	/* Render textured faces using cached texture */
	if (!ctx->jack_texture) {
		return;
	}

	/* Ensure renderer isn't blending our opaque faces */
	SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_NONE);

	/* Render faces in sorted order (back to front) */
	for (int oi = 0; oi < 6; oi++) {
		int f = fl[oi].idx;
		float avg_z = fl[oi].z;

		if (avg_z > -1.0f) {
			/* Get face quad corners */
			SDL_Vertex verts[4];
			for (int v = 0; v < 4; v++) {
				verts[v].position.x = projected[faces[f][v]][0];
				verts[v].position.y = projected[faces[f][v]][1];
				verts[v].color.r = 255;
				verts[v].color.g = 255;
				verts[v].color.b = 255;
				verts[v].color.a = 255;
			}

			/* UV coordinates - map texture to quad */
			verts[0].tex_coord.x = 0.0f; verts[0].tex_coord.y = 0.0f;
			verts[1].tex_coord.x = 1.0f; verts[1].tex_coord.y = 0.0f;
			verts[2].tex_coord.x = 1.0f; verts[2].tex_coord.y = 1.0f;
			verts[3].tex_coord.x = 0.0f; verts[3].tex_coord.y = 1.0f;

			/* Render two triangles to form the quad */
			int indices[6] = {0, 1, 2, 0, 2, 3};
			SDL_RenderGeometry(ctx->renderer, ctx->jack_texture, verts, 4, indices, 6);
		}
	}

	/* Restore blend mode for other rendering */
	SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
}

/* Tunnel effect */
void render_tunnel(DemoContext *ctx)
{
	float t = ctx->time;

	/* Make the tunnel eye move in a semi-elliptic pattern */
	float eye_x = WIDTH / 2 + cos(t * 0.5) * 120.0;
	float eye_y = HEIGHT / 2 + sin(t * 0.7) * 60.0;

	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			float dx = x - eye_x;
			float dy = y - eye_y;

			float distance = sqrt(dx * dx + dy * dy);
			float angle = atan2(dy, dx);

			float u = t * 0.5 + 10.0 / distance;
			float v = angle / PI + t * 0.2;

			int texture_x = (int)(u * 100.0) & 0xFF;
			int texture_y = (int)(v * 100.0) & 0xFF;

			int pattern = (texture_x ^ texture_y);

			int r = (pattern & 0xFF);
			int g = ((pattern << 2) & 0xFF);
			int b = ((pattern << 4) & 0xFF);

			float vignette = 1.0 - (distance / (WIDTH / 2));
			if (vignette < 0) vignette = 0;

			r = (int)(r * vignette);
			g = (int)(g * vignette);
			b = (int)(b * vignette);

			ctx->pixels[y * WIDTH + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
		}
	}
}

/* 3D star ball that bounces */
void render_star_ball(DemoContext *ctx)
{
	/* Generate sphere vertices using fibonacci sphere */
	#define NUM_BALL_STARS 200
	#define NUM_BG_STARS 150
	static float sphere_points[NUM_BALL_STARS][3];
	static int initialized = 0;
	static float ball_x = 400.0f;
	static float ball_y = 300.0f;
	static float vel_x = 3.0f;
	static float vel_y = 2.5f;
	static float squash_x = 1.0f;
	static float squash_y = 1.0f;

	/* Parallax background stars (3 layers) */
	typedef struct {
		float x, y;
		int layer;  /* 0=far, 1=mid, 2=near */
		int brightness;
	} BgStar;
	static BgStar bg_stars[NUM_BG_STARS];
	static int bg_initialized = 0;

	if (!initialized) {
		/* Generate points on sphere using fibonacci spiral */
		float phi = (1.0f + sqrtf(5.0f)) / 2.0f;  /* Golden ratio */
		for (int i = 0; i < NUM_BALL_STARS; i++) {
			float t = (float)i / NUM_BALL_STARS;
			float inc = acosf(1.0f - 2.0f * t);
			float azi = 2.0f * PI * i / phi;

			sphere_points[i][0] = sinf(inc) * cosf(azi);
			sphere_points[i][1] = sinf(inc) * sinf(azi);
			sphere_points[i][2] = cosf(inc);
		}
		initialized = 1;
	}

	if (!bg_initialized) {
		/* Initialize background stars with random positions */
		for (int i = 0; i < NUM_BG_STARS; i++) {
			bg_stars[i].x = (float)(rand() % WIDTH);
			bg_stars[i].y = (float)(rand() % HEIGHT);
			bg_stars[i].layer = i % 3;  /* Distribute across 3 layers */
			/* Fainter stars for farther layers */
			bg_stars[i].brightness = (bg_stars[i].layer == 0) ? 60 :
			                         (bg_stars[i].layer == 1) ? 90 : 120;
		}
		bg_initialized = 1;
	}

	/* Clear to black */
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		ctx->pixels[i] = 0xFF000000;
	}

	/* Render and update parallax background stars (scrolling opposite to text) */
	float scroll_speed = 180.0f;  /* Match text scroll speed */
	for (int i = 0; i < NUM_BG_STARS; i++) {
		/* Different speeds per layer for parallax effect */
		float layer_speed = (bg_stars[i].layer == 0) ? 0.2f :
		                    (bg_stars[i].layer == 1) ? 0.4f : 0.6f;

		/* Scroll left (opposite of text which scrolls left to right when viewing) */
		bg_stars[i].x += scroll_speed * layer_speed * 0.016f;

		/* Wrap around */
		if (bg_stars[i].x > WIDTH) {
			bg_stars[i].x = 0;
		}

		/* Draw star */
		int sx = (int)bg_stars[i].x;
		int sy = (int)bg_stars[i].y;
		if (sx >= 0 && sx < WIDTH && sy >= 0 && sy < HEIGHT) {
			int b = bg_stars[i].brightness;
			Uint32 color = 0xFF000000 | (b << 16) | (b << 8) | b;
			ctx->pixels[sy * WIDTH + sx] = color;
		}
	}

	/* Render horizontal raster bars behind the ball */
	float t = ctx->time;
	int num_bars = 6;
	for (int i = 0; i < num_bars; i++) {
		/* Calculate bar position with sine wave motion */
		float base_y = (i * HEIGHT / num_bars) + sinf(t * 1.2f + i * 0.9f) * 60.0f;
		int bar_height = 50;  /* Fatter bars */

		/* HSV to RGB for rainbow effect */
		float hue = (i / (float)num_bars + t * 0.15f);
		hue = hue - floorf(hue);  /* Keep in 0-1 range */

		int h_section = (int)(hue * 6);
		float f = hue * 6 - h_section;
		int v = 160;  /* Dimmer so ball and stars stand out */
		int p = 0;
		int q = (int)(v * (1 - f));
		int t_val = (int)(v * f);

		int r, g, b;
		switch (h_section % 6) {
		case 0: r = v; g = t_val; b = p; break;
		case 1: r = q; g = v; b = p; break;
		case 2: r = p; g = v; b = t_val; break;
		case 3: r = p; g = q; b = v; break;
		case 4: r = t_val; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
		}

		/* Draw bar with gradient */
		for (int dy = 0; dy < bar_height; dy++) {
			int y = (int)base_y + dy;
			if (y >= 0 && y < HEIGHT - 100) {  /* Leave room for scroll text */
				/* Gradient brightness based on position in bar */
				float brightness = 1.0f - fabsf(dy - bar_height / 2.0f) / (bar_height / 2.0f);
				brightness = brightness * brightness;  /* Squared for sharper falloff */

				int br = (int)(r * brightness);
				int bg = (int)(g * brightness);
				int bb = (int)(b * brightness);

				Uint32 color = 0xFF000000 | (br << 16) | (bg << 8) | bb;
				for (int x = 0; x < WIDTH; x++) {
					ctx->pixels[y * WIDTH + x] = color;
				}
			}
		}
	}

	/* Update ball position with physics */
	ball_x += vel_x;
	ball_y += vel_y;

	float radius = 80.0f;
	float squash_intensity = 0.15f;
	float recovery_speed = 0.2f;

	/* Bounce off edges with squash */
	if (ball_x - radius < 0 || ball_x + radius > WIDTH) {
		vel_x = -vel_x;
		ball_x = (ball_x < WIDTH / 2) ? radius : WIDTH - radius;
		squash_x = 1.0f - squash_intensity;  /* Squash horizontally */
		squash_y = 1.0f + squash_intensity;  /* Stretch vertically */
	}
	if (ball_y - radius < 0 || ball_y + radius > HEIGHT) {
		vel_y = -vel_y;
		ball_y = (ball_y < HEIGHT / 2) ? radius : HEIGHT - radius;
		squash_y = 1.0f - squash_intensity;  /* Squash vertically */
		squash_x = 1.0f + squash_intensity;  /* Stretch horizontally */
	}

	/* Recover to normal shape */
	squash_x += (1.0f - squash_x) * recovery_speed;
	squash_y += (1.0f - squash_y) * recovery_speed;

	/* Rotation angles */
	float rot_x = ctx->time * 0.7f;
	float rot_y = ctx->time * 0.5f;
	float rot_z = ctx->time * 0.3f;

	/* Render sphere points */
	for (int i = 0; i < NUM_BALL_STARS; i++) {
		float x = sphere_points[i][0] * radius;
		float y = sphere_points[i][1] * radius;
		float z = sphere_points[i][2] * radius;

		/* Apply squash and stretch */
		x *= squash_x;
		y *= squash_y;

		/* Rotate around X axis */
		float y1 = y * cosf(rot_x) - z * sinf(rot_x);
		float z1 = y * sinf(rot_x) + z * cosf(rot_x);
		y = y1;
		z = z1;

		/* Rotate around Y axis */
		float x1 = x * cosf(rot_y) + z * sinf(rot_y);
		z1 = -x * sinf(rot_y) + z * cosf(rot_y);
		x = x1;
		z = z1;

		/* Rotate around Z axis */
		float x2 = x * cosf(rot_z) - y * sinf(rot_z);
		float y2 = x * sinf(rot_z) + y * cosf(rot_z);
		x = x2;
		y = y2;

		/* Project to 2D */
		float depth = 200.0f / (200.0f + z);
		int sx = (int)(ball_x + x * depth);
		int sy = (int)(ball_y + y * depth);

		/* Color based on depth (closer = brighter) */
		int brightness = (int)(128 + 127 * depth);
		if (brightness < 0) brightness = 0;
		if (brightness > 255) brightness = 255;

		/* Draw star with size based on depth */
		if (sx >= 1 && sx < WIDTH - 1 && sy >= 1 && sy < HEIGHT - 1) {
			Uint32 color = 0xFF000000 | (brightness << 16) | (brightness << 8) | brightness;

			/* Larger stars for closer points */
			if (z > 0) {
				ctx->pixels[sy * WIDTH + sx] = color;
				ctx->pixels[sy * WIDTH + sx - 1] = color;
				ctx->pixels[sy * WIDTH + sx + 1] = color;
				ctx->pixels[(sy - 1) * WIDTH + sx] = color;
				ctx->pixels[(sy + 1) * WIDTH + sx] = color;
			} else {
				ctx->pixels[sy * WIDTH + sx] = color;
			}
		}
	}

	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);
}

/* Rotozoomer effect with texture rotation and zoom */
void render_rotozoomer(DemoContext *ctx)
{
	/* Clear to black */
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		ctx->pixels[i] = 0xFF000000;
	}

	if (!ctx->jack_surface) {
		SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
		SDL_RenderClear(ctx->renderer);
		SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);
		return;
	}

	float t = ctx->time;

	/* Rotation angle and zoom factor */
	float angle = t * 0.5f;
	float zoom = 1.5f + sinf(t * 0.7f) * 0.8f;  /* Breathing zoom */

	/* Center point with drift */
	float center_x = WIDTH / 2.0f + sinf(t * 0.3f) * 40.0f;
	float center_y = HEIGHT / 2.0f + cosf(t * 0.4f) * 30.0f;

	/* Precompute rotation matrix */
	float cos_a = cosf(angle);
	float sin_a = sinf(angle);

	int tex_w = ctx->jack_surface->w;
	int tex_h = ctx->jack_surface->h;

	/* Render rotozoomer */
	for (int y = 0; y < HEIGHT; y++) {
		for (int x = 0; x < WIDTH; x++) {
			/* Translate to center */
			float dx = (x - center_x) / zoom;
			float dy = (y - center_y) / zoom;

			/* Rotate */
			float u = dx * cos_a - dy * sin_a;
			float v = dx * sin_a + dy * cos_a;

			/* Translate to texture space and wrap */
			int tx = ((int)(u + tex_w / 2.0f) % tex_w + tex_w) % tex_w;
			int ty = ((int)(v + tex_h / 2.0f) % tex_h + tex_h) % tex_h;

			/* Sample texture */
			Uint32 color = get_jack_pixel(ctx->jack_surface, tx, ty);
			ctx->pixels[y * WIDTH + x] = color;
		}
	}

	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);

	/* Starball temporarily disabled - hard to see with Jack background */
	#if 0
	/* Now render the bouncing starball on top */
	/* Extract starball rendering code inline */
	#define NUM_BALL_STARS 200
	static float sphere_points[NUM_BALL_STARS][3];
	static int initialized = 0;
	static float ball_x = 400.0f;
	static float ball_y = 300.0f;
	static float vel_x = 3.0f;
	static float vel_y = 2.5f;
	static float squash_x = 1.0f;
	static float squash_y = 1.0f;

	if (!initialized) {
		/* Generate points on sphere using fibonacci spiral */
		float phi = (1.0f + sqrtf(5.0f)) / 2.0f;  /* Golden ratio */
		for (int i = 0; i < NUM_BALL_STARS; i++) {
			float t = (float)i / NUM_BALL_STARS;
			float inc = acosf(1.0f - 2.0f * t);
			float azi = 2.0f * PI * i / phi;

			sphere_points[i][0] = sinf(inc) * cosf(azi);
			sphere_points[i][1] = sinf(inc) * sinf(azi);
			sphere_points[i][2] = cosf(inc);
		}
		initialized = 1;
	}

	/* Update ball position with physics */
	ball_x += vel_x;
	ball_y += vel_y;

	float radius = 80.0f;
	float squash_intensity = 0.15f;
	float recovery_speed = 0.2f;

	/* Bounce off edges with squash */
	if (ball_x - radius < 0 || ball_x + radius > WIDTH) {
		vel_x = -vel_x;
		ball_x = (ball_x < WIDTH / 2) ? radius : WIDTH - radius;
		squash_x = 1.0f - squash_intensity;
		squash_y = 1.0f + squash_intensity;
	}
	if (ball_y - radius < 0 || ball_y + radius > HEIGHT) {
		vel_y = -vel_y;
		ball_y = (ball_y < HEIGHT / 2) ? radius : HEIGHT - radius;
		squash_y = 1.0f - squash_intensity;
		squash_x = 1.0f + squash_intensity;
	}

	/* Recover to normal shape */
	squash_x += (1.0f - squash_x) * recovery_speed;
	squash_y += (1.0f - squash_y) * recovery_speed;

	/* Rotation angles */
	float rot_x = ctx->time * 0.7f;
	float rot_y = ctx->time * 0.5f;
	float rot_z = ctx->time * 0.3f;

	/* Render sphere points with additive blending */
	SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_ADD);

	for (int i = 0; i < NUM_BALL_STARS; i++) {
		float x = sphere_points[i][0] * radius;
		float y = sphere_points[i][1] * radius;
		float z = sphere_points[i][2] * radius;

		/* Apply squash and stretch */
		x *= squash_x;
		y *= squash_y;

		/* Rotate around X axis */
		float y1 = y * cosf(rot_x) - z * sinf(rot_x);
		float z1 = y * sinf(rot_x) + z * cosf(rot_x);
		y = y1;
		z = z1;

		/* Rotate around Y axis */
		float x1 = x * cosf(rot_y) + z * sinf(rot_y);
		z1 = -x * sinf(rot_y) + z * cosf(rot_y);
		x = x1;
		z = z1;

		/* Rotate around Z axis */
		float x2 = x * cosf(rot_z) - y * sinf(rot_z);
		float y2 = x * sinf(rot_z) + y * cosf(rot_z);
		x = x2;
		y = y2;

		/* Project to 2D */
		float depth = 200.0f / (200.0f + z);
		int sx = (int)(ball_x + x * depth);
		int sy = (int)(ball_y + y * depth);

		/* Color based on depth (closer = brighter) */
		int brightness = (int)(128 + 127 * depth);
		if (brightness < 0) brightness = 0;
		if (brightness > 255) brightness = 255;

		/* Draw star with glow effect */
		if (sx >= 1 && sx < WIDTH - 1 && sy >= 1 && sy < HEIGHT - 1) {
			/* Larger stars for closer points with glow */
			if (z > 0) {
				SDL_SetRenderDrawColor(ctx->renderer, brightness, brightness, brightness, 255);
				/* Draw cross pattern for glow */
				SDL_RenderDrawPoint(ctx->renderer, sx, sy);
				SDL_RenderDrawPoint(ctx->renderer, sx - 1, sy);
				SDL_RenderDrawPoint(ctx->renderer, sx + 1, sy);
				SDL_RenderDrawPoint(ctx->renderer, sx, sy - 1);
				SDL_RenderDrawPoint(ctx->renderer, sx, sy + 1);
			} else {
				SDL_SetRenderDrawColor(ctx->renderer, brightness, brightness, brightness, 255);
				SDL_RenderDrawPoint(ctx->renderer, sx, sy);
			}
		}
	}

	SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
	#endif  /* Starball disabled */
}

/* Checkered floor perspective effect */
void render_checkered_floor(DemoContext *ctx)
{
	/* Clear to dark blue/purple sky gradient */
	for (int y = 0; y < HEIGHT; y++) {
		int r = 0;
		int g = (int)(20 + (y / (float)HEIGHT) * 30);
		int b = (int)(40 + (y / (float)HEIGHT) * 60);
		Uint32 color = 0xFF000000 | (r << 16) | (g << 8) | b;
		for (int x = 0; x < WIDTH; x++) {
			ctx->pixels[y * WIDTH + x] = color;
		}
	}

	/* Floor parameters */
	float horizon_y = HEIGHT * 0.6f;  /* Horizon line - upper part of screen */
	float floor_z_far = 50.0f;        /* Far distance */
	float tile_size = 0.8f;           /* Checkerboard tile size for floor casting */

	/* Floor casting with proper scrolling (based on lodev.org algorithm) */

	/* Camera/player position for scrolling */
	static float posX = 0.0f;
	static float posY = 0.0f;
	posY += 3.0f * 0.016f;  /* Scroll forward - slower to match ball */

	/* Camera direction (looking straight ahead) */
	float dirX = 0.0f;
	float dirY = 1.0f;

	/* Camera plane (for FOV) */
	float planeX = 0.66f;
	float planeY = 0.0f;

	for (int y = (int)horizon_y; y < HEIGHT; y++) {
		/* Ray directions for leftmost and rightmost rays */
		float rayDirX0 = dirX - planeX;
		float rayDirY0 = dirY - planeY;
		float rayDirX1 = dirX + planeX;
		float rayDirY1 = dirY + planeY;

		/* Calculate row distance (vertical screen position to floor distance) */
		int p = y - HEIGHT / 2;

		/* Skip the center horizon line to avoid division by zero */
		if (p == 0) continue;

		float posZ = 0.5f * HEIGHT;
		float rowDistance = posZ / p;

		/* Calculate floor step (how much texture coords change per screen X pixel) */
		float floorStepX = rowDistance * (rayDirX1 - rayDirX0) / WIDTH;
		float floorStepY = rowDistance * (rayDirY1 - rayDirY0) / WIDTH;

		/* Starting floor position for this row */
		float floorX = posX + rowDistance * rayDirX0;
		float floorY = posY + rowDistance * rayDirY0;

		for (int x = 0; x < WIDTH; x++) {
			/* Get checkerboard tile coordinates */
			/* Add small offset at center to avoid symmetry artifacts */
			float checkX = floorX;
			if (x == WIDTH / 2) checkX += 0.01f;

			int cellX = (int)floorf(checkX / tile_size);
			int cellY = (int)floorf(floorY / tile_size);

			/* Checkerboard pattern */
			int checker = (cellX + cellY) & 1;

			/* Distance fog */
			float fog = 1.0f - fminf(rowDistance / floor_z_far, 0.7f);

			int brightness = checker ? (int)(255 * fog) : (int)(50 * fog);

			Uint32 color = 0xFF000000 | (brightness << 16) | (brightness << 8) | brightness;
			ctx->pixels[y * WIDTH + x] = color;

			/* Advance to next pixel */
			floorX += floorStepX;
			floorY += floorStepY;
		}
	}

	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);

	/* Now render the bouncing starball on top */
	#define NUM_FLOOR_BALL_STARS 200
	static float sphere_points[NUM_FLOOR_BALL_STARS][3];
	static int initialized = 0;
	static float ball_x = 400.0f;
	static float ball_y = 0.0f;   /* Will be set above horizon on first run */
	static float vel_x = 2.0f;    /* Calmer horizontal movement */
	static float vel_y = 0.0f;  /* Vertical velocity for bounce */

	if (!initialized) {
		/* Generate points on sphere using fibonacci spiral */
		float phi = (1.0f + sqrtf(5.0f)) / 2.0f;  /* Golden ratio */
		for (int i = 0; i < NUM_FLOOR_BALL_STARS; i++) {
			float t = (float)i / NUM_FLOOR_BALL_STARS;
			float inc = acosf(1.0f - 2.0f * t);
			float azi = 2.0f * PI * i / phi;

			sphere_points[i][0] = sinf(inc) * cosf(azi);
			sphere_points[i][1] = sinf(inc) * sinf(azi);
			sphere_points[i][2] = cosf(inc);
		}
		ball_y = horizon_y - 100.0f;  /* Initialize well above horizon */
		vel_y = -300.0f;  /* Give it initial upward velocity to start bouncing */
		initialized = 1;
	}

	float radius = 70.0f;         /* Bigger ball */
	float gravity = 1000.0f;      /* Stronger gravity for livelier bounces */
	float bounce_damping = 0.85f; /* Less damping = bouncier */

	/* Update physics */
	vel_y += gravity * 0.016f;  /* Apply gravity */
	ball_y += vel_y * 0.016f;

	/* Bounce on floor - the floor is at the horizon line */
	if (ball_y + radius > horizon_y) {
		ball_y = horizon_y - radius;
		vel_y = -vel_y * bounce_damping;
		/* Add a small energy boost to keep it bouncing forever */
		if (fabsf(vel_y) < 500.0f) {
			vel_y -= 100.0f;  /* Add upward velocity if bounce is getting weak */
		}
	}

	/* Move horizontally */
	ball_x += vel_x;

	/* Bounce off screen left/right edges */
	if (ball_x - radius < 0 || ball_x + radius > WIDTH) {
		vel_x = -vel_x;
		ball_x = (ball_x < WIDTH / 2) ? radius : WIDTH - radius;
	}

	/* Rotation angles - calmer spin */
	float rot_x = ctx->time * 0.6f;
	float rot_y = ctx->time * 0.5f;
	float rot_z = ctx->time * 0.3f;

	/* Render sphere points with additive blending */
	SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_ADD);

	for (int i = 0; i < NUM_FLOOR_BALL_STARS; i++) {
		float x = sphere_points[i][0] * radius;
		float y = sphere_points[i][1] * radius;
		float z = sphere_points[i][2] * radius;

		/* Rotate around X axis */
		float y1 = y * cosf(rot_x) - z * sinf(rot_x);
		float z1 = y * sinf(rot_x) + z * cosf(rot_x);
		y = y1;
		z = z1;

		/* Rotate around Y axis */
		float x1 = x * cosf(rot_y) + z * sinf(rot_y);
		z1 = -x * sinf(rot_y) + z * cosf(rot_y);
		x = x1;
		z = z1;

		/* Rotate around Z axis */
		float x2 = x * cosf(rot_z) - y * sinf(rot_z);
		float y2 = x * sinf(rot_z) + y * cosf(rot_z);
		x = x2;
		y = y2;

		/* Project to 2D */
		float depth = 200.0f / (200.0f + z);
		int sx = (int)(ball_x + x * depth);
		int sy = (int)(ball_y + y * depth);

		/* Color based on depth */
		int brightness = (int)(150 + 105 * depth);
		if (brightness < 0) brightness = 0;
		if (brightness > 255) brightness = 255;

		/* Draw star with glow */
		if (sx >= 1 && sx < WIDTH - 1 && sy >= 1 && sy < HEIGHT - 1) {
			if (z > 0) {
				SDL_SetRenderDrawColor(ctx->renderer, brightness, brightness, brightness, 255);
				SDL_RenderDrawPoint(ctx->renderer, sx, sy);
				SDL_RenderDrawPoint(ctx->renderer, sx - 1, sy);
				SDL_RenderDrawPoint(ctx->renderer, sx + 1, sy);
				SDL_RenderDrawPoint(ctx->renderer, sx, sy - 1);
				SDL_RenderDrawPoint(ctx->renderer, sx, sy + 1);
			} else {
				SDL_SetRenderDrawColor(ctx->renderer, brightness, brightness, brightness, 255);
				SDL_RenderDrawPoint(ctx->renderer, sx, sy);
			}
		}
	}

	SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
}

/* Bouncing logo effect with squash and stretch */
void render_bouncing_logo(DemoContext *ctx)
{
	static float squash_x = 1.0f;  /* Horizontal scale factor */
	static float squash_y = 1.0f;  /* Vertical scale factor */
//	static float prev_x = -1.0f;   /* Previous x position */
	static float prev_y = -1.0f;   /* Previous y position */

	/* Clear to dark blue background */
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		ctx->pixels[i] = 0xFF001020;
	}

	if (!ctx->logo_texture) {
		SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
		SDL_RenderClear(ctx->renderer);
		SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);
		return;
	}

	/* Get logo dimensions */
	int logo_w, logo_h;
	SDL_QueryTexture(ctx->logo_texture, NULL, NULL, &logo_w, &logo_h);

	/* Bouncing physics with sine waves for smooth motion */
	float t = ctx->time;
	float bounce_x = sin(t * 0.8) * (WIDTH - logo_w) / 2 + (WIDTH - logo_w) / 2;
	float bounce_y = fabs(sin(t * 1.1)) * (HEIGHT - logo_h - 50) + 25;

	/* Detect edge collisions by checking velocity direction changes */
	float squash_intensity = 0.1f;  /* How much to squash (0.1 = 10% compression) */
	float recovery_speed = 0.25f;   /* How fast to recover to normal */

	/* Check horizontal collision (left/right edges) */
	/*if (prev_x >= 0) {
		float dx = bounce_x - prev_x;
		// Detect direction change = wall hit
		if ((prev_x <= 5 && dx > 0) || (prev_x >= WIDTH - logo_w - 5 && dx < 0)) {
			squash_x = 1.0f - squash_intensity;  // Squash horizontally
			squash_y = 1.0f + squash_intensity;  // Stretch vertically
		}
	}*/

	/* Check vertical collision (top/bottom edges) */
	if (prev_y >= 0) {
		float dy = bounce_y - prev_y;
		/* Detect direction change = floor/ceiling hit */
		if ((prev_y <= 30 && dy > 0) || (prev_y >= HEIGHT - logo_h - 30 && dy < 0)) {
			squash_y = 1.0f - squash_intensity;  /* Squash vertically */
			squash_x = 1.0f + squash_intensity;  /* Stretch horizontally */
		}
	}

	/* Smoothly recover to normal scale */
	squash_x += (1.0f - squash_x) * recovery_speed;
	squash_y += (1.0f - squash_y) * recovery_speed;

	/* Clamp to prevent overshoot */
	if (fabs(squash_x - 1.0f) < 0.01f) squash_x = 1.0f;
	if (fabs(squash_y - 1.0f) < 0.01f) squash_y = 1.0f;

	/* Store position for next frame */
//	prev_x = bounce_x;
	prev_y = bounce_y;

	/* Add some gentle rotation */
	float rotation = sin(t * 0.5) * 8.0;  /* ±8 degrees */

	/* Apply squash and stretch to dimensions */
	int scaled_w = (int)(logo_w * squash_x);
	int scaled_h = (int)(logo_h * squash_y);

	/* Center the scaled logo at the bounce position */
	SDL_Rect dest_rect = {
		(int)(bounce_x + (logo_w - scaled_w) / 2),
		(int)(bounce_y + (logo_h - scaled_h) / 2),
		scaled_w,
		scaled_h
	};

	/* Update background texture */
	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);

	/* Render the rotating, squashing logo */
	SDL_RenderCopyEx(ctx->renderer, ctx->logo_texture, NULL, &dest_rect,
	                 rotation, NULL, SDL_FLIP_NONE);
}

/* Raining logo effect - logo falls in line by line from bottom to top */
void render_raining_logo(DemoContext *ctx)
{
	/* Animation phases */
	#define PHASE_RAIN_IN 0
	#define PHASE_SETTLE 1
	#define PHASE_WOBBLE 2
	#define PHASE_RAIN_OUT 3
	#define PHASE_PAUSE 4

	static int current_phase = PHASE_RAIN_IN;
	static float phase_time = 0.0f;

	/* Clear to dark blue background */
	for (int i = 0; i < WIDTH * HEIGHT; i++) {
		ctx->pixels[i] = 0xFF001020;
	}

	if (!ctx->logo_texture) {
		SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
		SDL_RenderClear(ctx->renderer);
		SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);
		return;
	}

	int logo_w, logo_h;
	SDL_QueryTexture(ctx->logo_texture, NULL, NULL, &logo_w, &logo_h);

	/* Update animation time */
	float dt = 0.016f;  /* Assume 60 fps */
	phase_time += dt;

	/* Phase transitions */
	switch (current_phase) {
	case PHASE_RAIN_IN:
		if (phase_time > 2.0f) {  /* 2 seconds to rain in */
			current_phase = PHASE_SETTLE;
			phase_time = 0.0f;
		}
		break;
	case PHASE_SETTLE:
		if (phase_time > 0.3f) {  /* 0.3 seconds settling */
			current_phase = PHASE_WOBBLE;
			phase_time = 0.0f;
		}
		break;
	case PHASE_WOBBLE:
		if (phase_time > 1.5f) {  /* 1.5 seconds wobbling */
			current_phase = PHASE_RAIN_OUT;
			phase_time = 0.0f;
		}
		break;
	case PHASE_RAIN_OUT:
		if (phase_time > 2.0f) {  /* 2 seconds to rain out */
			current_phase = PHASE_PAUSE;
			phase_time = 0.0f;
		}
		break;
	case PHASE_PAUSE:
		if (phase_time > 0.5f) {  /* 0.5 second pause */
			current_phase = PHASE_RAIN_IN;
			phase_time = 0.0f;
		}
		break;
	}

	/* Render background */
	SDL_UpdateTexture(ctx->texture, NULL, ctx->pixels, WIDTH * sizeof(Uint32));
	SDL_RenderClear(ctx->renderer);
	SDL_RenderCopy(ctx->renderer, ctx->texture, NULL, NULL);

	/* Calculate logo position */
	int base_x = (WIDTH - logo_w) / 2;
	int base_y = (HEIGHT - logo_h) / 2;

	if (current_phase == PHASE_RAIN_IN) {
		/* Logo falls down from above screen, bottom lines fall first */
		/* Each line has a delay based on its position from bottom */
		for (int line = 0; line < logo_h; line++) {
			int src_y = line;
			/* Bottom lines start falling first - smaller delay for bottom */
			float delay = (float)(logo_h - line - 1) * 0.005f;
			float line_time = phase_time - delay;

			if (line_time > 0) {
				/* Calculate fall position with gravity (y = 0.5 * g * t^2) */
				float gravity = 400.0f;
				float y_pos = -logo_h + src_y + (0.5f * gravity * line_time * line_time);

				/* Stop at target position */
				float target = base_y + src_y;
				if (y_pos > target) y_pos = target;

				SDL_Rect src = { 0, src_y, logo_w, 1 };
				SDL_Rect dst = { base_x, (int)y_pos, logo_w, 1 };
				SDL_RenderCopy(ctx->renderer, ctx->logo_texture, &src, &dst);
			}
		}
	}
	else if (current_phase == PHASE_SETTLE) {
		/* Slight bounce */
		float settle = exp(-phase_time * 10.0f) * sin(phase_time * 30.0f) * 5.0f;
		SDL_Rect dst = { base_x, base_y + (int)settle, logo_w, logo_h };
		SDL_RenderCopy(ctx->renderer, ctx->logo_texture, NULL, &dst);
	}
	else if (current_phase == PHASE_WOBBLE) {
		/* Jelly wobble - each line wobbles horizontally with different phase */
		for (int line = 0; line < logo_h; line++) {
			/* Sine wave wobble based on line position */
			float wobble_phase = (float)line / logo_h * 3.14159f * 2.0f;
			float wobble = sin(phase_time * 5.0f + wobble_phase) * 8.0f;
			/* Dampen over time */
			float dampen = exp(-phase_time * 1.5f);
			wobble *= dampen;

			SDL_Rect src = { 0, line, logo_w, 1 };
			SDL_Rect dst = { base_x + (int)wobble, base_y + line, logo_w, 1 };
			SDL_RenderCopy(ctx->renderer, ctx->logo_texture, &src, &dst);
		}
	}
	else if (current_phase == PHASE_RAIN_OUT) {
		/* Rain out through bottom, top lines fall first with gravity */
		for (int line = 0; line < logo_h; line++) {
			int src_y = line;
			/* Top lines start falling first */
			float delay = (float)line * 0.005f;
			float line_time = phase_time - delay;

			if (line_time > 0) {
				/* Calculate fall position with gravity */
				float gravity = 400.0f;
				float start_y = base_y + src_y;
				float y_pos = start_y + (0.5f * gravity * line_time * line_time);

				/* Only render if still visible or partially visible */
				if (y_pos < HEIGHT) {
					SDL_Rect src = { 0, src_y, logo_w, 1 };
					SDL_Rect dst = { base_x, (int)y_pos, logo_w, 1 };
					SDL_RenderCopy(ctx->renderer, ctx->logo_texture, &src, &dst);
				}
			} else {
				/* Not falling yet, render at normal position */
				SDL_Rect src = { 0, src_y, logo_w, 1 };
				SDL_Rect dst = { base_x, base_y + src_y, logo_w, 1 };
				SDL_RenderCopy(ctx->renderer, ctx->logo_texture, &src, &dst);
			}
		}
	}
}

/* Helper to calculate spaces generated by SKIP command */
static int calculate_skip_spaces(float skip_screens)
{
	return (int)(skip_screens * (WIDTH / 35.0f));
}

/* Build a map of control code positions in the stripped text */
typedef struct {
	int position;  /* Character position in stripped text */
	float pixel_position;  /* Pixel position based on actual glyph widths */
	char type;     /* P=pause, S=speed, T=style, C=color, K=skip */
	char data[64]; /* Parameter data */
} ControlCode;

static ControlCode control_codes[256];
static int num_control_codes = 0;

static void build_control_map(const char *text)
{
	if (!text)
		return;

	num_control_codes = 0;
	const char *p = text;
	int char_pos = 0;  /* Position in stripped text */

	while (*p && num_control_codes < 256) {
		if (*p == '{') {
			const char *start = p + 1;
			const char *end = strchr(start, '}');

			if (end) {
				int len = end - start;
				if (len > 0 && len < 64) {
					char cmd[64] = {0};
					strncpy(cmd, start, len);

					ControlCode *cc = &control_codes[num_control_codes];
					cc->position = char_pos;
					cc->pixel_position = -1.0f;  /* Will be calculated later with actual font metrics */

					if (strncmp(cmd, "PAUSE:", 6) == 0) {
						cc->type = 'P';
						strncpy(cc->data, cmd + 6, 63);
						num_control_codes++;
					} else if (strncmp(cmd, "SPEED:", 6) == 0) {
						cc->type = 'S';
						strncpy(cc->data, cmd + 6, 63);
						num_control_codes++;
					} else if (strncmp(cmd, "STYLE:", 6) == 0) {
						cc->type = 'T';
						strncpy(cc->data, cmd + 6, 63);
						num_control_codes++;
					} else if (strncmp(cmd, "COLOR:", 6) == 0) {
						cc->type = 'C';
						strncpy(cc->data, cmd + 6, 63);
						num_control_codes++;
					} else if (strncmp(cmd, "SKIP:", 5) == 0) {
						/* SKIP is added to control codes for pixel position calculation */
						cc->type = 'K';
						strncpy(cc->data, cmd + 5, 63);
						num_control_codes++;
						/* Advance char_pos by the spaces that SKIP will insert */
						float skip_screens = atof(cmd + 5);
						char_pos += calculate_skip_spaces(skip_screens);
					}
				}
				p = end + 1;
				continue;
			}
		}
		if (*p != '{' && *p != '}')
			char_pos++;
		p++;
	}
}


/* Apply control codes based on current scroll position */
static void apply_scroll_controls(DemoContext *ctx, float scroll_offset, float total_width)
{
	/* Track which control codes have been triggered */
	static int triggered[256] = {0};
	static int last_num_codes = 0;
	static int last_cycle = -1;

	/* Reset triggered flags if control codes changed */
	if (num_control_codes != last_num_codes) {
		for (int i = 0; i < 256; i++)
			triggered[i] = 0;
		last_num_codes = num_control_codes;
	}

	/* Detect wrapping by tracking which cycle we're in */
	int current_cycle = (int)(scroll_offset / total_width);
	if (current_cycle != last_cycle && last_cycle >= 0) {
		/* We've wrapped to a new cycle - reset all triggered flags */
		for (int i = 0; i < 256; i++)
			triggered[i] = 0;
	}
	last_cycle = current_cycle;

	/* Calculate scroll position within current cycle */
	float cycle_offset = fmodf(scroll_offset, total_width);

	/* Apply control codes that we've just reached (not yet triggered) */
	for (int i = 0; i < num_control_codes; i++) {
		ControlCode *cc = &control_codes[i];
		float trigger_offset = 500.0f;

		/* Trigger when we pass the pixel position plus offset (only once per cycle) */
		if (!triggered[i] && cycle_offset >= cc->pixel_position + trigger_offset) {
			triggered[i] = 1;

			switch (cc->type) {
			case 'P': /* PAUSE */
				{
					/* Use strtof for safer parsing with error checking */
					char *endptr;
					float pause_sec = strtof(cc->data, &endptr);
					if (endptr != cc->data && pause_sec > 0) {
						ctx->scroll_pause_until = ctx->global_time + pause_sec;
					}
				}
				break;

			case 'S': /* SPEED */
				{
					char *endptr;
					float new_speed = strtof(cc->data, &endptr);
					if (endptr != cc->data && new_speed >= 0)
						ctx->scroll_speed = new_speed;
				}
				break;

			case 'T': /* STYLE */
				if (strcmp(cc->data, "wave") == 0)
					ctx->scroll_style = SCROLL_SINE_WAVE;
				else if (strcmp(cc->data, "roller") == 0)
					ctx->scroll_style = SCROLL_ROLLER_3D;
				else if (strcmp(cc->data, "classic") == 0)
					ctx->scroll_style = SCROLL_CLASSIC;
				else if (strcmp(cc->data, "bounce") == 0)
					ctx->scroll_style = SCROLL_BOUNCE;
				break;

			case 'C': /* COLOR */
				{
					int r, g, b;
					if (sscanf(cc->data, "%d,%d,%d", &r, &g, &b) == 3) {
						ctx->scroll_color[0] = r;
						ctx->scroll_color[1] = g;
						ctx->scroll_color[2] = b;
					}
				}
				break;
			}
		}
	}
}

/* Remove control codes from text for display */
static char *strip_control_codes(const char *text)
{
	if (!text)
		return NULL;

	/* First pass: calculate required size accounting for SKIP expansions */
	int expanded_len = 0;
	const char *p = text;
	while (*p) {
		if (*p == '{') {
			const char *end = strchr(p, '}');
			if (end) {
				int cmd_len = end - p - 1;
				char cmd[64] = {0};
				if (cmd_len > 0 && cmd_len < 64) {
					strncpy(cmd, p + 1, cmd_len);
					/* Check if this is a SKIP command */
					if (strncmp(cmd, "SKIP:", 5) == 0) {
						float skip_screens = atof(cmd + 5);
						expanded_len += calculate_skip_spaces(skip_screens);
					}
				}
				p = end + 1;
				continue;
			}
		}
		expanded_len++;
		p++;
	}

	/* Allocate result with room for expansions */
	char *result = malloc(expanded_len + 1);
	if (!result)
		return NULL;

	/* Second pass: copy text and expand SKIP commands */
	const char *src = text;
	char *dst = result;

	while (*src) {
		if (*src == '{') {
			const char *end = strchr(src, '}');
			if (end) {
				int cmd_len = end - src - 1;
				char cmd[64] = {0};
				if (cmd_len > 0 && cmd_len < 64) {
					strncpy(cmd, src + 1, cmd_len);
					/* Expand SKIP into spaces */
					if (strncmp(cmd, "SKIP:", 5) == 0) {
						float skip_screens = atof(cmd + 5);
						int spaces = calculate_skip_spaces(skip_screens);
						for (int i = 0; i < spaces; i++) {
							*dst++ = ' ';
						}
					}
				}
				src = end + 1;
				continue;
			}
		}
		*dst++ = *src++;
	}
	*dst = '\0';

	return result;
}

/* Scroll text rendering with different styles */
void render_scroll_text(DemoContext *ctx)
{
	const char *text = ctx->scroll_text;

	if (ctx->scroll_style == SCROLL_NONE || !text)
		return;

	/* Strip control codes for rendering and build control map */
	static char *display_text = NULL;
	static const char *last_text = NULL;
	static int needs_pixel_calc = 1;
	if (text != last_text) {
		free(display_text);
		display_text = strip_control_codes(text);
		build_control_map(text);  /* Build map when text changes */
		/* Pixel positions will be calculated later using the glyph cache */
		needs_pixel_calc = 1;  /* Flag that we need to recalculate pixel positions */
		last_text = text;
	}

	if (!display_text)
		return;

	int text_len = strlen(display_text);

	/* Update scroll offset - only advance when not paused */
	if (ctx->last_frame_time == 0.0f) {
		ctx->last_frame_time = ctx->global_time;
	}

	float delta_time = ctx->global_time - ctx->last_frame_time;
	ctx->last_frame_time = ctx->global_time;

	if (ctx->global_time >= ctx->scroll_pause_until) {
		ctx->scroll_offset += ctx->scroll_speed * delta_time;
	}

	if (ctx->scroll_style == SCROLL_SINE_WAVE || ctx->scroll_style == SCROLL_ROLLER_3D || ctx->scroll_style == SCROLL_BOUNCE) {
		/* Glyph cache with metrics */
		typedef struct {
			SDL_Texture *tex;
			SDL_Texture *tex_outline;
			int w, h;
			int adv;
			int valid;
		} Glyph;
		static Glyph gcache[256];
		static int initialized;
		static int total_adv;

		if (!initialized) {
			memset(gcache, 0, sizeof(gcache));
			TTF_SetFontKerning(ctx->font, 1);
			initialized = 1;
		}

		/* Sine wave scroller (transparent, for plasma) */
		float x_pos = WIDTH;

		for (int i = 0; i < text_len; i++) {
			char buffer[2] = {display_text[i], '\0'};
			unsigned char ch = (unsigned char)display_text[i];

			/* Render character if not cached */
			if (!gcache[ch].valid && ch >= 32 && ch < 127) {
				SDL_Color white = {255, 255, 255, 255};
				SDL_Surface *surface = TTF_RenderText_Blended(ctx->font, buffer, white);
				if (surface) {
					int minx, maxx, miny, maxy, advance;
					if (TTF_GlyphMetrics(ctx->font, ch, &minx, &maxx, &miny, &maxy, &advance) == 0)
						gcache[ch].adv = advance;
					else
						gcache[ch].adv = surface->w;
					gcache[ch].tex = SDL_CreateTextureFromSurface(ctx->renderer, surface);
					gcache[ch].w = surface->w;
					gcache[ch].h = surface->h;
					gcache[ch].valid = 1;
					SDL_FreeSurface(surface);

					/* Outline cache for 3D roller */
					if (ctx->font_outline && ctx->scroll_style == SCROLL_ROLLER_3D) {
						SDL_Color black = {0, 0, 0, 255};
						SDL_Surface *os = TTF_RenderText_Blended(ctx->font_outline, buffer, black);
						if (os) {
							gcache[ch].tex_outline = SDL_CreateTextureFromSurface(ctx->renderer, os);
							SDL_FreeSurface(os);
						}
					}
				}
			}

			float char_x = x_pos - ctx->scroll_offset;

			/* Calculate total advance once per frame, and pixel positions for control codes */
			if (i == 0) {
#ifdef DEBUG_CONTROL_CODES
				if (needs_pixel_calc) {
					printf("Control code pixel positions calculated:\n");
				}
#endif
				total_adv = 0;
				float pixel_pos = 0.0f;
				int cc_idx = 0;

				for (int k = 0; k < text_len; k++) {
					unsigned char ck = (unsigned char)display_text[k];

					/* Update control code pixel positions at this character position */
					if (needs_pixel_calc) {
						while (cc_idx < num_control_codes && control_codes[cc_idx].position == k) {
							control_codes[cc_idx].pixel_position = pixel_pos;
#ifdef DEBUG_CONTROL_CODES
							printf("  Position %d: pixel %.1f, type %c, data '%s'\n",
							       control_codes[cc_idx].position,
							       control_codes[cc_idx].pixel_position,
							       control_codes[cc_idx].type,
							       control_codes[cc_idx].data);
#endif
							cc_idx++;
						}
					}

					int adv = gcache[ck].valid ? gcache[ck].adv : 35;
					total_adv += adv;
					pixel_pos += adv;
#if SDL_TTF_VERSION_ATLEAST(2,0,18)
					if (k > 0) {
						unsigned char prev = (unsigned char)display_text[k - 1];
						int kern = TTF_GetFontKerningSizeGlyphs(ctx->font, prev, ck);
						total_adv += kern;
						pixel_pos += kern;
					}
#endif
				}

				/* Handle control codes at end of text */
				if (needs_pixel_calc) {
					while (cc_idx < num_control_codes) {
						control_codes[cc_idx].pixel_position = pixel_pos;
#ifdef DEBUG_CONTROL_CODES
						printf("  Position %d: pixel %.1f, type %c, data '%s'\n",
						       control_codes[cc_idx].position,
						       control_codes[cc_idx].pixel_position,
						       control_codes[cc_idx].type,
						       control_codes[cc_idx].data);
#endif
						cc_idx++;
					}
					needs_pixel_calc = 0;
				}
			}

			/* Wrap around */
			while (char_x < -100) char_x += total_adv;

			if (char_x > -100 && char_x < WIDTH + 100 && gcache[ch].valid) {
				float phase = ctx->global_time * 2.0f + i * 0.3f;
				float wave = sinf(phase) * 80.0f;
				int y_pos = HEIGHT / 2 + (int)wave;

				/* Update color - use custom color if set, otherwise gradient */
				Uint8 r, g, b;
				if (ctx->scroll_color[0] || ctx->scroll_color[1] || ctx->scroll_color[2]) {
					r = ctx->scroll_color[0];
					g = ctx->scroll_color[1];
					b = ctx->scroll_color[2];
				} else {
					int color_shift = (int)(ctx->global_time * 100 + i * 10) % 360;
					r = (Uint8)(128 + 127 * sin(color_shift * PI / 180));
					g = (Uint8)(128 + 127 * sin((color_shift + 120) * PI / 180));
					b = (Uint8)(128 + 127 * sin((color_shift + 240) * PI / 180));
				}

				if (ctx->scroll_style == SCROLL_ROLLER_3D) {
					/* 3D roller with scale, outline, and glow */
					float scale = 1.0f + 0.25f * cosf(phase);
					int dw = (int)(gcache[ch].w * scale);
					int dh = (int)(gcache[ch].h * scale);
					SDL_Rect dest = {(int)char_x, y_pos - dh / 2, dw, dh};

					/* Outline behind (configurable) */
					if (ctx->roller_effect == 0 || ctx->roller_effect == 3) {
						if (gcache[ch].tex_outline) {
							if (ctx->roller_effect == 3) {
								/* Color outline (thicker text effect) */
								SDL_SetTextureColorMod(gcache[ch].tex_outline, r, g, b);
							} else {
								/* Black outline (drop shadow) */
								SDL_SetTextureColorMod(gcache[ch].tex_outline, 0, 0, 0);
							}
							SDL_Rect od = dest;
							od.x -= 1;
							od.y -= 1;
							SDL_RenderCopy(ctx->renderer, gcache[ch].tex_outline, NULL, &od);
						}
					}

					/* Main glyph with color */
					SDL_SetTextureColorMod(gcache[ch].tex, r, g, b);
					SDL_RenderCopy(ctx->renderer, gcache[ch].tex, NULL, &dest);

					/* Soft glow (configurable) */
					if (ctx->roller_effect != 2) {
						SDL_SetTextureBlendMode(gcache[ch].tex, SDL_BLENDMODE_ADD);
						SDL_SetTextureAlphaMod(gcache[ch].tex, 40);
						SDL_Rect glow = dest;
						glow.x -= 2;
						glow.y -= 2;
						glow.w += 4;
						glow.h += 4;
						SDL_RenderCopy(ctx->renderer, gcache[ch].tex, NULL, &glow);
						SDL_SetTextureAlphaMod(gcache[ch].tex, 255);
						SDL_SetTextureBlendMode(gcache[ch].tex, SDL_BLENDMODE_BLEND);
					}
				} else if (ctx->scroll_style == SCROLL_BOUNCE) {
					/* Bouncing characters - each char bounces independently */
					float bounce_phase = ctx->global_time * 4.0f + i * 0.5f;
					/* Use abs(sin) to create bounce pattern (always positive) */
					float bounce_height = fabsf(sinf(bounce_phase)) * 60.0f;
					/* Add slight squash at bottom */
					float squash = 1.0f - (1.0f - fabsf(sinf(bounce_phase))) * 0.15f;

					int bounce_y = HEIGHT / 2 - (int)bounce_height;
					int dw = gcache[ch].w;
					int dh = (int)(gcache[ch].h * squash);

					SDL_SetTextureColorMod(gcache[ch].tex, r, g, b);
					SDL_Rect dest = {(int)char_x, bounce_y - dh / 2, dw, dh};
					SDL_RenderCopy(ctx->renderer, gcache[ch].tex, NULL, &dest);
				} else {
					/* Simple sine wave */
					SDL_SetTextureColorMod(gcache[ch].tex, r, g, b);
					SDL_Rect dest = {(int)char_x, y_pos - gcache[ch].h / 2, gcache[ch].w, gcache[ch].h};
					SDL_RenderCopy(ctx->renderer, gcache[ch].tex, NULL, &dest);
				}
			}

			/* Advance by glyph advance + kerning */
			int adv = gcache[ch].valid ? gcache[ch].adv : 35;
#if SDL_TTF_VERSION_ATLEAST(2,0,18)
			if (i > 0) {
				unsigned char prev = (unsigned char)text[i - 1];
				int kern = TTF_GetFontKerningSizeGlyphs(ctx->font, prev, ch);
				adv += kern;
			}
#endif
			x_pos += adv;
		}

		/* Apply control codes based on scroll position */
		apply_scroll_controls(ctx, ctx->scroll_offset, total_adv);
	} else if (ctx->scroll_style == SCROLL_CLASSIC) {
		/* Classic bottom scroller - render entire line once */
		static SDL_Texture *line_tex = NULL;
		static int line_w = 0;
		static const char *last_display_text = NULL;

		/* Rebuild texture if text changed */
		if (!line_tex || display_text != last_display_text) {
			if (line_tex) {
				SDL_DestroyTexture(line_tex);
				line_tex = NULL;
			}
			SDL_Color color = {255, 255, 100, 255};
			SDL_Surface *surface = TTF_RenderText_Blended(ctx->font, display_text, color);
			if (surface) {
				line_tex = SDL_CreateTextureFromSurface(ctx->renderer, surface);
				line_w = surface->w;
				SDL_FreeSurface(surface);
				last_display_text = display_text;

				/* Calculate pixel positions for control codes using glyph metrics */
				if (needs_pixel_calc) {
#ifdef DEBUG_CONTROL_CODES
					printf("Control code pixel positions calculated:\n");
#endif
					float pixel_pos = 0.0f;
					int cc_idx = 0;

					for (int k = 0; k < text_len; k++) {
						unsigned char ch = (unsigned char)display_text[k];

						/* Update control code pixel positions at this character position */
						while (cc_idx < num_control_codes && control_codes[cc_idx].position == k) {
							control_codes[cc_idx].pixel_position = pixel_pos;
#ifdef DEBUG_CONTROL_CODES
							printf("  Position %d: pixel %.1f, type %c, data '%s'\n",
							       control_codes[cc_idx].position,
							       control_codes[cc_idx].pixel_position,
							       control_codes[cc_idx].type,
							       control_codes[cc_idx].data);
#endif
							cc_idx++;
						}

						/* Get advance width for this character */
						if (ch >= 32 && ch < 127) {
							int minx, maxx, miny, maxy, advance;
							if (TTF_GlyphMetrics(ctx->font, ch, &minx, &maxx, &miny, &maxy, &advance) == 0) {
								pixel_pos += advance;
							} else {
								pixel_pos += 20;  /* Fallback */
							}
						} else {
							pixel_pos += 20;  /* Fallback for non-printable */
						}
					}

					/* Handle control codes at end of text */
					while (cc_idx < num_control_codes) {
						control_codes[cc_idx].pixel_position = pixel_pos;
#ifdef DEBUG_CONTROL_CODES
						printf("  Position %d: pixel %.1f, type %c, data '%s'\n",
						       control_codes[cc_idx].position,
						       control_codes[cc_idx].pixel_position,
						       control_codes[cc_idx].type,
						       control_codes[cc_idx].data);
#endif
						cc_idx++;
					}
					needs_pixel_calc = 0;
				}
			}
		}

		/* Apply control codes based on scroll position */
		apply_scroll_controls(ctx, ctx->scroll_offset, line_w);

		if (line_tex) {
			int y_pos = HEIGHT - 60;
			int h;
			SDL_QueryTexture(line_tex, NULL, NULL, NULL, &h);

			/* Scroll position */
			int scroll_x = (int)(WIDTH - ctx->scroll_offset);
			while (scroll_x < -line_w) scroll_x += line_w;

			/* Draw the line, wrapping around */
			SDL_Rect dest = {scroll_x, y_pos - h / 2, line_w, h};
			SDL_RenderCopy(ctx->renderer, line_tex, NULL, &dest);

			/* Draw wrapped copy if needed */
			if (scroll_x + line_w < WIDTH) {
				dest.x = scroll_x + line_w;
				SDL_RenderCopy(ctx->renderer, line_tex, NULL, &dest);
			}
		}
	}
}

static int usage(int rc)
{
	printf("Usage: demo [OPTIONS] [SCENE...]\n");
	printf("\nDisplay Options:\n");
	printf("  -f, --fullscreen   Run in fullscreen mode (scales to display)\n");
	printf("  -w, --window WxH   Set window size (e.g., 1920x1080)\n");
	printf("  -s, --scale N      Integer scaling (e.g., 2 = 1600x1200)\n");
	printf("\nPlayback Options:\n");
	printf("  -d, --duration SEC Scene duration in seconds (default: 15)\n");
	printf("  -t, --text FILE    Load scroll text from file\n");
	printf("  -r, --roller N     Roller effect: 0=all, 1=no outline, 2=clean, 3=color (default: 1)\n");
	printf("  -h, --help         Show this help message\n");
	printf("\nScenes:\n");
	printf("  0 - Starfield      3 - Tunnel           6 - 3D Star Ball\n");
	printf("  1 - Plasma         4 - Bouncing Logo    7 - Rotozoomer\n");
	printf("  2 - Cube           5 - Raining Logo     8 - Checkered Floor\n");
	printf("\nExamples:\n");
	printf("  demo -f              # Fullscreen, auto-cycle scenes\n");
	printf("  demo -s 2            # 2x window size (1600x1200)\n");
	printf("  demo -w 1920x1080    # Custom window size\n");
	printf("  demo -d 30 2 6       # Show cube & star ball, 30s each\n");
	printf("  demo -t /mnt/scroll.txt  # Custom scroll text\n");

	return rc;
}

int main(int argc, char *argv[])
{
	/* Window/display settings */
	int fullscreen = 0;
	int window_width = 0;   /* 0 = auto-detect */
	int window_height = 0;
	int scale_factor = 1;
	int auto_resolution = 1;  /* Auto-detect and adapt resolution */
	const char *scroll_file_path = NULL;
	int scene_list[6];
	int num_scenes = 0;
	int scene_duration = 15000;  /* Default: 15 seconds per scene */

	/* Default scroll text */
	const char *default_text = "Infix OS - The Container demo{PAUSE:2}"
		"    *** Greetings to the demoscene <3"
		"    *** Infix is API first: NETCONF + RESTCONF"
		"    *** Say Hi to our mascot, Jack! :-)"
		"    *** YANG is the real HERO tho ..."
		"    *** Sponsored by Wires in Westeros"
		"    *** From idea to production - we've got you!"
		"    *** Visit us at https://wires.se"
		"                                *** ";

	/* Parse command-line arguments */

	static struct option long_options[] = {
		{"help",       no_argument,       NULL, 'h'},
		{"duration",   required_argument, NULL, 'd'},
		{"fullscreen", no_argument,       NULL, 'f'},
		{"window",     required_argument, NULL, 'w'},
		{"scale",      required_argument, NULL, 's'},
		{"text",       required_argument, NULL, 't'},
		{"roller",     required_argument, NULL, 'r'},
		{NULL,         0,                 NULL, 0}
	};

	int opt;
	int roller_effect = 1;  /* Default: no outline, glow only */
	while ((opt = getopt_long(argc, argv, "hd:fw:s:t:r:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			return usage(0);

		case 'd':
			{
				int duration_sec = atoi(optarg);
				if (duration_sec > 0) {
					scene_duration = duration_sec * 1000;
				} else {
					fprintf(stderr, "Error: Invalid duration '%s'. Must be positive.\n", optarg);
					return 1;
				}
			}
			break;

		case 'f':
			fullscreen = 1;
			break;

		case 'w':
			if (sscanf(optarg, "%dx%d", &window_width, &window_height) != 2) {
				fprintf(stderr, "Error: Invalid window size '%s'. Use format WxH (e.g., 1920x1080)\n", optarg);
				return 1;
			}
			/* Adapt render resolution to match aspect ratio */
			{
				float aspect = (float)window_width / window_height;
				WIDTH = 800;
				HEIGHT = (int)(800.0f / aspect);
				auto_resolution = 0;  /* Manual window size disables auto-detection */
			}
			break;

		case 's':
			scale_factor = atoi(optarg);
			if (scale_factor < 1) {
				fprintf(stderr, "Error: Invalid scale factor '%s'. Must be >= 1\n", optarg);
				return 1;
			}
			window_width = WIDTH * scale_factor;
			window_height = HEIGHT * scale_factor;
			break;

		case 't':
			scroll_file_path = optarg;
			break;

		case 'r':
			roller_effect = atoi(optarg);
			if (roller_effect < 0 || roller_effect > 3) {
				fprintf(stderr, "Error: Invalid roller effect '%s'. Must be 0-3:\n", optarg);
				fprintf(stderr, "  0 = All effects (outline + glow)\n");
				fprintf(stderr, "  1 = No outline (glow only)\n");
				fprintf(stderr, "  2 = No outline/glow (clean)\n");
				fprintf(stderr, "  3 = Colored outline (thicker text)\n");
				return 1;
			}
			break;

		default:
			return usage(1);
		}
	}

	/* Parse non-option arguments as scene numbers */
	for (int i = optind; i < argc; i++) {
		int scene = atoi(argv[i]);
		if (scene >= 0 && scene <= 8) {
			if (num_scenes < 7) {
				scene_list[num_scenes++] = scene;
			}
		} else {
			fprintf(stderr, "Error: Invalid scene number '%s'. Use 0-8.\n", argv[i]);
			return 1;
		}
	}

	/* Initialize SDL and libraries */
	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}

	if (TTF_Init() < 0) {
		fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
		SDL_Quit();
		return 1;
	}

	if (IMG_Init(IMG_INIT_PNG) == 0) {
		fprintf(stderr, "IMG_Init failed: %s\n", IMG_GetError());
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	/* Initialize SDL_mixer for music */
	if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
		fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
		IMG_Quit();
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	/* Initialize demo context */
	DemoContext ctx = {0};
	ctx.fixed_scene = -1;  /* -1 means auto-switch scenes */
	ctx.current_scene_index = 0;
	ctx.scene_duration = scene_duration;
	ctx.scroll_speed = 180.0f;  /* Default scroll speed */
	ctx.scroll_pause_until = 0.0f;
	ctx.scroll_color[0] = 0;  /* 0,0,0 means use gradient */
	ctx.scroll_color[1] = 0;
	ctx.scroll_color[2] = 0;
	ctx.scroll_style = SCROLL_ROLLER_3D;  /* Default scroll style */
	ctx.scroll_offset = 0.0f;
	ctx.last_frame_time = 0.0f;
	ctx.roller_effect = roller_effect;

	/* Load scroll text from file or use default */
	if (scroll_file_path) {
		FILE *fp;

		fp = fopen(scroll_file_path, "r");
		if (fp) {
			size_t i, read_bytes = 0;
			long file_size;

			fseek(fp, 0, SEEK_END);
			file_size = ftell(fp);
			fseek(fp, 0, SEEK_SET);

			ctx.scroll_text = malloc(file_size + 1);
			if (ctx.scroll_text) {
				read_bytes = fread(ctx.scroll_text, 1, file_size, fp);
				ctx.scroll_text[read_bytes] = '\0';
			}
			fclose(fp);

			for (i = 0; i < read_bytes; i++) {
				if (ctx.scroll_text[i] == '\n' || ctx.scroll_text[i] == '\r')
					ctx.scroll_text[i] = ' ';
			}
		} else {
			fprintf(stderr, "Warning: Could not open '%s', using default text\n", scroll_file_path);
			ctx.scroll_text = strdup(default_text);
		}
	} else {
		ctx.scroll_text = strdup(default_text);
	}

	/* Handle scene selection */
	if (num_scenes == 1) {
		/* Single scene - fix to that scene */
		ctx.fixed_scene = scene_list[0];
		ctx.current_scene = scene_list[0];
		ctx.num_scenes = 0;  /* Fixed scene, no list */
	} else if (num_scenes > 1) {
		/* Multiple scenes specified - cycle through custom list */
		for (int i = 0; i < num_scenes; i++) {
			ctx.scene_list[i] = scene_list[i];
		}
		ctx.num_scenes = num_scenes;
		ctx.current_scene_index = 0;
		ctx.current_scene = ctx.scene_list[0];
	} else {
		/* No scenes specified - use default list (skip scenes 4 and 7) */
		ctx.scene_list[0] = 0;
		ctx.scene_list[1] = 1;
		ctx.scene_list[2] = 2;
		ctx.scene_list[3] = 3;
		ctx.scene_list[4] = 5;
		ctx.scene_list[5] = 6;
		ctx.scene_list[6] = 8;
		ctx.num_scenes = 7;
		ctx.current_scene_index = 0;
		ctx.current_scene = ctx.scene_list[0];
	}

	/* Set scaling quality hint before creating renderer */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

	/* Auto-detect display resolution and adapt if needed */
	if (auto_resolution || fullscreen) {
		SDL_DisplayMode dm;
		if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
			if (fullscreen) {
				/* Use native display resolution */
				window_width = dm.w;
				window_height = dm.h;
			} else if (window_width == 0) {
				/* Auto-size window to 80% of display */
				window_width = dm.w * 0.8;
				window_height = dm.h * 0.8;
			}

			/* Adapt internal resolution to match display aspect ratio */
			float display_aspect = (float)dm.w / dm.h;

			/* Always adapt to match display aspect ratio */
			WIDTH = 800;
			HEIGHT = (int)(800.0f / display_aspect);

			/* fprintf(stderr, "Display: %dx%d (aspect %.2f), Internal: %dx%d\n", */
			/*         dm.w, dm.h, display_aspect, WIDTH, HEIGHT); */
		}
	}

	/* Default window size if not set */
	if (window_width == 0) window_width = WIDTH;
	if (window_height == 0) window_height = HEIGHT;

	/* fprintf(stderr, "Window: %dx%d, Render: %dx%d\n", */
	/*         window_width, window_height, WIDTH, HEIGHT); */

	Uint32 window_flags = SDL_WINDOW_SHOWN;
	if (fullscreen) {
		window_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	ctx.window = SDL_CreateWindow("Infix Container Demo",
	                               SDL_WINDOWPOS_CENTERED,
	                               SDL_WINDOWPOS_CENTERED,
	                               window_width, window_height,
	                               window_flags);

	if (!ctx.window) {
		fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	ctx.renderer = SDL_CreateRenderer(ctx.window, -1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	/* Set logical rendering size - render at adapted resolution, display scales automatically */
	SDL_RenderSetLogicalSize(ctx.renderer, WIDTH, HEIGHT);

	ctx.texture = SDL_CreateTexture(ctx.renderer,
	                                SDL_PIXELFORMAT_ARGB8888,
	                                SDL_TEXTUREACCESS_STREAMING,
	                                WIDTH, HEIGHT);

	/* Load embedded Topaz-8 font from memory */
	SDL_RWops *font_rw = SDL_RWFromConstMem(topaz_8_otf, topaz_8_otf_len);
	if (!font_rw) {
		fprintf(stderr, "Failed to create RWops for font: %s\n", SDL_GetError());
		SDL_DestroyTexture(ctx.texture);
		SDL_DestroyRenderer(ctx.renderer);
		SDL_DestroyWindow(ctx.window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	ctx.font = TTF_OpenFontRW(font_rw, 1, 48);  /* 1 = automatically close RW */
	if (!ctx.font) {
		fprintf(stderr, "Failed to load embedded font: %s\n", TTF_GetError());
		SDL_DestroyTexture(ctx.texture);
		SDL_DestroyRenderer(ctx.renderer);
		SDL_DestroyWindow(ctx.window);
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	/* Load outline font for 3D roller effect */
	SDL_RWops *font_outline_rw = SDL_RWFromConstMem(topaz_8_otf, topaz_8_otf_len);
	if (font_outline_rw) {
		ctx.font_outline = TTF_OpenFontRW(font_outline_rw, 1, 48);
		if (ctx.font_outline) {
			TTF_SetFontOutline(ctx.font_outline, 2);
		}
	}

	ctx.pixels = malloc(WIDTH * HEIGHT * sizeof(Uint32));

	/* Create plasma texture (lower resolution for performance) */
	ctx.plasma_texture = SDL_CreateTexture(ctx.renderer,
	                                       SDL_PIXELFORMAT_ARGB8888,
	                                       SDL_TEXTUREACCESS_STREAMING,
	                                       400, 300);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  /* Nearest neighbor for retro look */

	/* Load embedded jack.png from memory */
	SDL_RWops *image_rw = SDL_RWFromConstMem(jack_png, jack_png_len);
	if (!image_rw) {
		fprintf(stderr, "Warning: Failed to create RWops for image: %s\n", SDL_GetError());
		fprintf(stderr, "Cube will render without texture.\n");
		ctx.jack_texture = NULL;
		ctx.jack_surface = NULL;
	} else {
		ctx.jack_surface = IMG_Load_RW(image_rw, 1);  /* 1 = automatically close RW */
		if (!ctx.jack_surface) {
			fprintf(stderr, "Warning: Failed to load embedded image: %s\n", IMG_GetError());
			fprintf(stderr, "Cube will render without texture.\n");
			ctx.jack_texture = NULL;
		} else {
			/* Convert to RGB888 format to strip alpha channel */
			SDL_Surface *converted = SDL_ConvertSurfaceFormat(ctx.jack_surface, SDL_PIXELFORMAT_RGB888, 0);
			if (converted) {
				SDL_FreeSurface(ctx.jack_surface);
				ctx.jack_surface = converted;
			}
			/* Create and cache the texture */
			ctx.jack_texture = SDL_CreateTextureFromSurface(ctx.renderer, ctx.jack_surface);
			SDL_SetTextureBlendMode(ctx.jack_texture, SDL_BLENDMODE_NONE);
			SDL_SetTextureAlphaMod(ctx.jack_texture, 255);
		}
	}

	/* Load embedded logo.png from memory */
	SDL_RWops *logo_rw = SDL_RWFromConstMem(logo_png, logo_png_len);
	if (!logo_rw) {
		fprintf(stderr, "Warning: Failed to create RWops for logo: %s\n", SDL_GetError());
		fprintf(stderr, "Bouncing logo scene will not render.\n");
		ctx.logo_texture = NULL;
		ctx.logo_surface = NULL;
	} else {
		ctx.logo_surface = IMG_Load_RW(logo_rw, 1);  /* 1 = automatically close RW */
		if (!ctx.logo_surface) {
			fprintf(stderr, "Warning: Failed to load embedded logo: %s\n", IMG_GetError());
			fprintf(stderr, "Bouncing logo scene will not render.\n");
			ctx.logo_texture = NULL;
		} else {
			/* Create and cache the texture */
			ctx.logo_texture = SDL_CreateTextureFromSurface(ctx.renderer, ctx.logo_surface);
			SDL_SetTextureBlendMode(ctx.logo_texture, SDL_BLENDMODE_BLEND);
		}
	}

	/* Initialize starfield */
	for (int i = 0; i < NUM_STARS; i++) {
		ctx.stars[i].x = (rand() % 2000 - 1000) / 10.0f;
		ctx.stars[i].y = (rand() % 2000 - 1000) / 10.0f;
		ctx.stars[i].z = (rand() % 10000) / 100.0f;
	}

	/* Load and play music from embedded data */
#ifdef HAVE_MUSIC
	SDL_RWops *music_rw = SDL_RWFromConstMem(music_mod, music_mod_len);
	if (music_rw) {
		Mix_Music *music = Mix_LoadMUS_RW(music_rw, 1);  /* 1 = auto-free RW */
		if (music) {
			Mix_PlayMusic(music, -1);  /* -1 = loop forever */
			Mix_VolumeMusic(MIX_MAX_VOLUME / 2);  /* 50% volume */
		} else {
			fprintf(stderr, "Warning: Failed to load music: %s\n", Mix_GetError());
		}
	}
#endif

	int running = 1;
	Uint32 start_time = SDL_GetTicks();
	Uint32 scene_start = start_time;

	while (running) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT) {
				running = 0;
			}
			if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
				running = 0;
			}
		}

		Uint32 current_time = SDL_GetTicks();
		ctx.time = (current_time - scene_start) / 1000.0f;
		ctx.global_time = (current_time - start_time) / 1000.0f;

		/* Handle scene transitions with fade (only if not fixed) */
		if (ctx.fixed_scene == -1) {
			Uint32 scene_duration = current_time - scene_start;
			float fade_duration = 300.0f;  /* 300ms fade */

			if (scene_duration > ctx.scene_duration) {
				/* Start fade out */
				if (!ctx.fading) {
					ctx.fading = 1;
					ctx.fade_alpha = 1.0f;
				}

				float fade_progress = (scene_duration - ctx.scene_duration) / fade_duration;

				if (fade_progress < 1.0f) {
					/* Fade out */
					ctx.fade_alpha = 1.0f - fade_progress;
				} else if (fade_progress < 2.0f) {
					/* Switch scene and fade in */
					if (ctx.fade_alpha < 0.5f) {
						/* Advance to next scene in the list */
						if (ctx.num_scenes > 0) {
							ctx.current_scene_index = (ctx.current_scene_index + 1) % ctx.num_scenes;
							ctx.current_scene = ctx.scene_list[ctx.current_scene_index];
						}
						scene_start = current_time - (Uint32)fade_duration;
						ctx.time = 0;
					}
					ctx.fade_alpha = fade_progress - 1.0f;
				} else {
					/* Fade complete */
					ctx.fade_alpha = 1.0f;
					ctx.fading = 0;
					scene_start = current_time;
					ctx.time = 0;
				}
			} else {
				ctx.fade_alpha = 1.0f;
				ctx.fading = 0;
			}
		} else {
			ctx.fade_alpha = 1.0f;
		}

		/* Render current scene */
		switch (ctx.current_scene) {
		case 0:
			render_starfield(&ctx);
			render_scroll_text(&ctx);
			break;
		case 1:
			render_plasma(&ctx);
			SDL_RenderClear(ctx.renderer);
			SDL_RenderCopy(ctx.renderer, ctx.plasma_texture, NULL, NULL);
			render_scroll_text(&ctx);
			break;
		case 2:
			render_cube(&ctx);
			render_scroll_text(&ctx);
			break;
		case 3:
			render_tunnel(&ctx);
			SDL_UpdateTexture(ctx.texture, NULL, ctx.pixels, WIDTH * sizeof(Uint32));
			SDL_RenderClear(ctx.renderer);
			SDL_RenderCopy(ctx.renderer, ctx.texture, NULL, NULL);
			render_scroll_text(&ctx);
			break;
		case 4:
			render_bouncing_logo(&ctx);
			render_scroll_text(&ctx);
			break;
		case 5:
			render_raining_logo(&ctx);
			render_scroll_text(&ctx);
			break;
		case 6:
			render_star_ball(&ctx);
			render_scroll_text(&ctx);
			break;
		case 7:
			render_rotozoomer(&ctx);
			render_scroll_text(&ctx);
			break;
		case 8:
			render_checkered_floor(&ctx);
			render_scroll_text(&ctx);
			break;
		}

		/* Apply fade effect */
		if (ctx.fade_alpha < 1.0f) {
			SDL_SetRenderDrawBlendMode(ctx.renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(ctx.renderer, 0, 0, 0, (Uint8)((1.0f - ctx.fade_alpha) * 255));
			SDL_Rect fade_rect = {0, 0, WIDTH, HEIGHT};
			SDL_RenderFillRect(ctx.renderer, &fade_rect);
		}

		SDL_RenderPresent(ctx.renderer);
		SDL_Delay(16);
	}

	free(ctx.pixels);
	free(ctx.scroll_text);
	if (ctx.jack_surface) {
		SDL_FreeSurface(ctx.jack_surface);
	}
	if (ctx.jack_texture) {
		SDL_DestroyTexture(ctx.jack_texture);
	}
	if (ctx.logo_surface) {
		SDL_FreeSurface(ctx.logo_surface);
	}
	if (ctx.logo_texture) {
		SDL_DestroyTexture(ctx.logo_texture);
	}
	if (ctx.plasma_texture) {
		SDL_DestroyTexture(ctx.plasma_texture);
	}
	TTF_CloseFont(ctx.font);
	if (ctx.font_outline) {
		TTF_CloseFont(ctx.font_outline);
	}
	SDL_DestroyTexture(ctx.texture);
	SDL_DestroyRenderer(ctx.renderer);
	SDL_DestroyWindow(ctx.window);
	Mix_CloseAudio();
	IMG_Quit();
	TTF_Quit();
	SDL_Quit();

	return 0;
}
