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

/* Embedded font, image, and music data */
#include "font_data.h"
#include "image_data.h"
#include "logo_data.h"

/* Music data will be included when available */
#ifdef HAVE_MUSIC
#include "music_data.h"
#endif

#define WIDTH 800
#define HEIGHT 600
#define PI 3.14159265358979323846
#define NUM_STARS 200
#define MAX_LOGO_PARTICLES 8192

typedef enum {
    SCROLL_NONE,
    SCROLL_SINE_WAVE,
    SCROLL_BOTTOM_TRADITIONAL
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
    int scene_list[6];      /* Custom scene order */
    int num_scenes;         /* Number of scenes in list */
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

/* Scroll text rendering with different styles */
void render_scroll_text(DemoContext *ctx)
{
	const char *text = " Infix OS  <>  The Container demo <>"
		"    *** Greetings to the demoscene <3"
		"    *** NETCONF and RESTCONF APIs"
		"    *** SAY HI TO JACK! :-)      "
		"    *** YANG is the real HERO tho ..."
		"    *** Sponsored by Wires in Westeros ***"
		"    "
		    ;

	if (ctx->scroll_style == SCROLL_NONE) {
		return;
	}

	int text_len = strlen(text);
	float scroll_speed = 180.0;
	float scroll_offset = ctx->global_time * scroll_speed;

	if (ctx->scroll_style == SCROLL_SINE_WAVE) {
		/* Glyph cache with metrics */
		typedef struct {
			SDL_Texture *tex;
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
			char buffer[2] = {text[i], '\0'};
			unsigned char ch = (unsigned char)text[i];

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
				}
			}

			float char_x = x_pos - scroll_offset;

			/* Calculate total advance once per frame */
			if (i == 0) {
				total_adv = 0;
				for (int k = 0; k < text_len; k++) {
					unsigned char ck = (unsigned char)text[k];
					total_adv += gcache[ck].valid ? gcache[ck].adv : 35;
#if SDL_TTF_VERSION_ATLEAST(2,0,18)
					if (k > 0) {
						unsigned char prev = (unsigned char)text[k - 1];
						total_adv += TTF_GetFontKerningSizeGlyphs(ctx->font, prev, ck);
					}
#endif
				}
			}

			/* Wrap around */
			while (char_x < -100) char_x += total_adv;

			if (char_x > -100 && char_x < WIDTH + 100 && gcache[ch].valid) {
				float wave = sin(ctx->global_time * 2.0 + i * 0.3) * 80.0;
				int y_pos = HEIGHT / 2 + (int)wave;

				/* Update color for gradient effect */
				int color_shift = (int)(ctx->global_time * 100 + i * 10) % 360;
				Uint8 r = (Uint8)(128 + 127 * sin(color_shift * PI / 180));
				Uint8 g = (Uint8)(128 + 127 * sin((color_shift + 120) * PI / 180));
				Uint8 b = (Uint8)(128 + 127 * sin((color_shift + 240) * PI / 180));
				SDL_SetTextureColorMod(gcache[ch].tex, r, g, b);

				SDL_Rect dest = {(int)char_x, y_pos - gcache[ch].h / 2, gcache[ch].w, gcache[ch].h};
				SDL_RenderCopy(ctx->renderer, gcache[ch].tex, NULL, &dest);
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
	} else if (ctx->scroll_style == SCROLL_BOTTOM_TRADITIONAL) {
		/* Traditional bottom scroller - render entire line once */
		static SDL_Texture *line_tex = NULL;
		static int line_w = 0;

		if (!line_tex) {
			SDL_Color color = {255, 255, 100, 255};
			SDL_Surface *surface = TTF_RenderText_Blended(ctx->font, text, color);
			if (surface) {
				line_tex = SDL_CreateTextureFromSurface(ctx->renderer, surface);
				line_w = surface->w;
				SDL_FreeSurface(surface);
			}
		}

		if (line_tex) {
			int y_pos = HEIGHT - 60;
			int h;
			SDL_QueryTexture(line_tex, NULL, NULL, NULL, &h);

			/* Scroll position */
			int scroll_x = (int)(WIDTH - scroll_offset);
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

int main(int argc, char *argv[])
{
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

	DemoContext ctx = {0};
	ctx.fixed_scene = -1;  /* -1 means auto-switch scenes */
	ctx.current_scene_index = 0;
	int scene_list[6];
	int num_scenes = 0;

	/* Parse command-line arguments */
	ctx.scene_duration = 15000;  /* Default: 15 seconds per scene */

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: %s [OPTIONS] [SCENE...]\n", argv[0]);
			printf("\nOptions:\n");
			printf("  -h, --help        Show this help message\n");
			printf("  -d, --duration SEC Set scene duration in seconds (default: 15)\n");
			printf("\nScenes:\n");
			printf("  0 - Starfield\n");
			printf("  1 - Plasma\n");
			printf("  2 - Cube\n");
			printf("  3 - Tunnel\n");
			printf("  4 - Bouncing Logo (hidden - manual only)\n");
			printf("  5 - Raining Logo\n");
			printf("\nExamples:\n");
			printf("  %s           # Auto-cycle through scenes 0-3 and 5\n", argv[0]);
			printf("  %s 2         # Show only cube scene\n", argv[0]);
			printf("  %s 1 3 5     # Cycle between plasma, tunnel, and raining logo\n", argv[0]);
			printf("  %s -d 30     # Auto-cycle with 30 second scenes\n", argv[0]);
			printf("  %s -d 10 2 5 # Cycle cube and raining logo, 10 sec each\n", argv[0]);
			IMG_Quit();
			TTF_Quit();
			SDL_Quit();
			return 0;
		}
		else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--duration") == 0) {
			if (i + 1 >= argc) {
				fprintf(stderr, "Error: %s requires an argument\n", argv[i]);
				IMG_Quit();
				TTF_Quit();
				SDL_Quit();
				return 1;
			}
			int duration_sec = atoi(argv[++i]);
			if (duration_sec > 0) {
				ctx.scene_duration = duration_sec * 1000;
			} else {
				fprintf(stderr, "Error: Invalid duration '%s'. Must be positive.\n", argv[i]);
				IMG_Quit();
				TTF_Quit();
				SDL_Quit();
				return 1;
			}
		}
		else {
			/* Non-option argument - treat as scene number */
			int scene = atoi(argv[i]);
			if (scene >= 0 && scene <= 5) {
				if (num_scenes < 6) {
					scene_list[num_scenes++] = scene;
				}
			} else {
				fprintf(stderr, "Error: Invalid scene number '%s'. Use 0-5.\n", argv[i]);
				IMG_Quit();
				TTF_Quit();
				SDL_Quit();
				return 1;
			}
		}
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
		/* No scenes specified - use default list (skip scene 4) */
		ctx.scene_list[0] = 0;
		ctx.scene_list[1] = 1;
		ctx.scene_list[2] = 2;
		ctx.scene_list[3] = 3;
		ctx.scene_list[4] = 5;
		ctx.num_scenes = 5;
		ctx.current_scene_index = 0;
		ctx.current_scene = ctx.scene_list[0];
	}

	ctx.window = SDL_CreateWindow("Infix Container Demo",
	                               SDL_WINDOWPOS_CENTERED,
	                               SDL_WINDOWPOS_CENTERED,
	                               WIDTH, HEIGHT,
	                               SDL_WINDOW_SHOWN);

	if (!ctx.window) {
		fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
		TTF_Quit();
		SDL_Quit();
		return 1;
	}

	ctx.renderer = SDL_CreateRenderer(ctx.window, -1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
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
			ctx.scroll_style = SCROLL_SINE_WAVE;
			render_starfield(&ctx);
			render_scroll_text(&ctx);
			break;
		case 1:
			ctx.scroll_style = SCROLL_SINE_WAVE;
			render_plasma(&ctx);
			SDL_RenderClear(ctx.renderer);
			SDL_RenderCopy(ctx.renderer, ctx.plasma_texture, NULL, NULL);
			render_scroll_text(&ctx);
			break;
		case 2:
			ctx.scroll_style = SCROLL_BOTTOM_TRADITIONAL;
			render_cube(&ctx);
			render_scroll_text(&ctx);
			break;
		case 3:
			ctx.scroll_style = SCROLL_BOTTOM_TRADITIONAL;
			render_tunnel(&ctx);
			SDL_UpdateTexture(ctx.texture, NULL, ctx.pixels, WIDTH * sizeof(Uint32));
			SDL_RenderClear(ctx.renderer);
			SDL_RenderCopy(ctx.renderer, ctx.texture, NULL, NULL);
			render_scroll_text(&ctx);
			break;
		case 4:
			ctx.scroll_style = SCROLL_BOTTOM_TRADITIONAL;
			render_bouncing_logo(&ctx);
			render_scroll_text(&ctx);
			break;
		case 5:
			ctx.scroll_style = SCROLL_BOTTOM_TRADITIONAL;
			render_raining_logo(&ctx);
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
	SDL_DestroyTexture(ctx.texture);
	SDL_DestroyRenderer(ctx.renderer);
	SDL_DestroyWindow(ctx.window);
	Mix_CloseAudio();
	IMG_Quit();
	TTF_Quit();
	SDL_Quit();

	return 0;
}
