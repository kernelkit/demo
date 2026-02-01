# Build stage
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc \
    make \
    pkg-config \
    libgtk-3-dev \
    libwebkit2gtk-4.1-dev \
    libsoup-3.0-dev \
    libcjson-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY breeze.c weather.c weather.h animations.c animations.h \
     sunriset.c sunriset.h Makefile ./

RUN make

# Runtime stage
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    libgtk-3-0 \
    libwebkit2gtk-4.1-0 \
    libsoup-3.0-0 \
    libcjson1 \
    ca-certificates \
    xorg \
    xinit \
    dbus-x11 \
    x11-xserver-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/breeze .
COPY xorg.conf /etc/X11/xorg.conf
COPY start.sh /app/start.sh

ENV DISPLAY=:0

CMD ["/app/start.sh", "-f"]
