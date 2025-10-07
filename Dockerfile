# Build stage
FROM alpine:latest AS builder

RUN apk add --no-cache \
    gcc \
    musl-dev \
    make \
    pkgconf \
    sdl2-dev \
    sdl2_ttf-dev \
    sdl2_image-dev \
    sdl2_mixer-dev

WORKDIR /build

COPY demo.c Makefile topaz-8.otf *.png music.mod* ./

RUN make

# Runtime stage
FROM alpine:latest

RUN apk add --no-cache \
    sdl2 \
    sdl2_ttf \
    sdl2_image \
    sdl2_mixer \
    libpulse \
    libxmp \
    libdrm \
    mesa-dri-gallium \
    mesa-gbm \
    mesa-egl \
    mesa-gl \
    mesa-gles \
    xorg-server \
    xf86-video-modesetting \
    xf86-input-libinput \
    xdpyinfo \
    xinit

WORKDIR /app

# Copy compiled binary from builder
COPY --from=builder /build/demo .

# Copy X11 config and startup script
COPY xorg.conf /etc/X11/xorg.conf
COPY start.sh /app/start.sh

ENV DISPLAY=:0

CMD ["/app/start.sh", "-f"]
