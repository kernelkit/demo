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

/* Music data will be included when available */
#ifdef HAVE_MUSIC
#include "music_data.h"
#endif

#define WIDTH 800
#define HEIGHT 600
#define PI 3.14159265358979323846
#define NUM_STARS 200

typedef enum {
    SCROLL_NONE,
    SCROLL_SINE_WAVE,
    SCROLL_BOTTOM_TRADITIONAL
} ScrollStyle;

typedef struct {
    float x, y, z;
} Star;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_Texture *plasma_texture;
    Uint32 *pixels;
    TTF_Font *font;
    SDL_Surface *jack_surface;
    SDL_Texture *jack_texture;
    int current_scene;
    int fixed_scene;
    float time;
    float global_time;
    float fade_alpha;
    int fading;
    ScrollStyle scroll_style;
    Star stars[NUM_STARS];
} DemoContext;

/* Plasma effect - optimized with lower resolution and LUT */
void render_plasma(DemoContext *ctx) {
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
void render_starfield(DemoContext *ctx) {
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
void render_scroller(DemoContext *ctx) {
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
Uint32 get_jack_pixel(SDL_Surface *surface, int x, int y) {
    if (!surface || x < 0 || y < 0 || x >= surface->w || y >= surface->h) {
        return 0xFF000000;
    }
    Uint32 *pixels = (Uint32 *)surface->pixels;
    return pixels[y * surface->w + x];
}

/* Rotating cube with texture mapped faces and copper bars */
void render_cube(DemoContext *ctx) {
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
void render_tunnel(DemoContext *ctx) {
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

/* Scroll text rendering with different styles */
void render_scroll_text(DemoContext *ctx) {
    const char *text = "    INFIX - CONTAINER DEMO    *** GREETINGS TO THE DEMOSCENE ***    "
                       " NETCONF *** RESTCONF *** YANG *** SAY HI TO JACK! ***    ";

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

int main(int argc, char *argv[]) {
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

    /* Parse command-line arguments */
    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("Usage: %s [scene]\n", argv[0]);
            printf("Scenes:\n");
            printf("  0 - Plasma\n");
            printf("  1 - Scroller\n");
            printf("  2 - Cube\n");
            printf("  3 - Tunnel\n");
            printf("\nIf no scene is specified, auto-switches between all scenes.\n");
            IMG_Quit();
            TTF_Quit();
            SDL_Quit();
            return 0;
        }
        int scene = atoi(argv[1]);
        if (scene >= 0 && scene <= 3) {
            ctx.fixed_scene = scene;
            ctx.current_scene = scene;
        } else {
            fprintf(stderr, "Invalid scene number. Use 0-3.\n");
            IMG_Quit();
            TTF_Quit();
            SDL_Quit();
            return 1;
        }
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

            if (scene_duration > 10000) {
                /* Start fade out */
                if (!ctx.fading) {
                    ctx.fading = 1;
                    ctx.fade_alpha = 1.0f;
                }

                float fade_progress = (scene_duration - 10000) / fade_duration;

                if (fade_progress < 1.0f) {
                    /* Fade out */
                    ctx.fade_alpha = 1.0f - fade_progress;
                } else if (fade_progress < 2.0f) {
                    /* Switch scene and fade in */
                    if (ctx.fade_alpha < 0.5f) {
                        ctx.current_scene = (ctx.current_scene + 1) % 4;
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
