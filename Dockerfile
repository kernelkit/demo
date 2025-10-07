FROM alpine:latest

RUN apk add --no-cache \
    sdl2 \
    sdl2-dev \
    sdl2_ttf \
    sdl2_ttf-dev \
    sdl2_image \
    sdl2_image-dev \
    sdl2_mixer \
    sdl2_mixer-dev \
    libpulse \
    libxmp \
    mesa-dri-gallium \
    mesa-gbm \
    gcc \
    musl-dev \
    make \
    vim

WORKDIR /app

COPY demo.c Makefile topaz-8.otf *.png music.mod* ./

RUN make

# Default to X11, but can be overridden for framebuffer
ENV DISPLAY=:0
ENV SDL_VIDEODRIVER=x11

CMD ["./demo"]
