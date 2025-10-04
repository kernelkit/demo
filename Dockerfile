FROM alpine:latest

RUN apk add --no-cache \
    sdl2 \
    sdl2-dev \
    sdl2_ttf \
    sdl2_ttf-dev \
    ttf-dejavu \
    gcc \
    musl-dev \
    make

WORKDIR /app
COPY demo.c .

RUN gcc -o demo demo.c -lSDL2 -lSDL2_ttf -lm -O2

ENV DISPLAY=:0

CMD ["./demo"]
