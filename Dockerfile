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
    gcc \
    musl-dev \
    make \
    vim

WORKDIR /app

COPY demo.c Makefile topaz-8.otf *.png music.mod* ./

RUN make

ENV DISPLAY=:0

CMD ["./demo"]
