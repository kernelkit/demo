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
    xinit \
    gcc \
    musl-dev \
    make \
    vim

WORKDIR /app

COPY demo.c Makefile topaz-8.otf *.png music.mod* ./

RUN make

# Create minimal X config for GPU acceleration
RUN mkdir -p /etc/X11 && cat > /etc/X11/xorg.conf << 'EOF'
Section "ServerFlags"
    Option "DontVTSwitch" "true"
    Option "BlankTime" "0"
    Option "StandbyTime" "0"
    Option "SuspendTime" "0"
    Option "OffTime" "0"
EndSection

Section "Device"
    Identifier "Card0"
    Driver "modesetting"
EndSection

Section "Screen"
    Identifier "Screen0"
    Device "Card0"
EndSection
EOF

# Create startup script
RUN cat > /app/start.sh << 'EOF'
#!/bin/sh
# Smart startup: use existing X or start our own

cleanup()
{
    echo "Shutting down..."
    if [ -n "$DEMOPID" ]; then
        kill $DEMOPID 2>/dev/null
        wait $DEMOPID 2>/dev/null
    fi
    if [ -n "$XPID" ]; then
        kill $XPID 2>/dev/null
        wait $XPID 2>/dev/null
    fi
    exit 0
}

# Set up signal handlers
trap cleanup TERM INT

# Check if X server is already available
if xdpyinfo -display "${DISPLAY:-:0}" >/dev/null 2>&1; then
    echo "Using existing X server on $DISPLAY"
    exec ./demo "$@"
else
    echo "No X server found, starting embedded X server..."
    # Start X server in background
    Xorg -noreset +extension GLX +extension RANDR +extension RENDER -logfile /tmp/xorg.log -config /etc/X11/xorg.conf :0 &
    XPID=$!
    sleep 2

    # Run demo in background so we can handle signals
    DISPLAY=:0 ./demo "$@" &
    DEMOPID=$!

    # Wait for demo to finish
    wait $DEMOPID
    EXITCODE=$?

    # Clean up X server
    kill $XPID 2>/dev/null
    wait $XPID 2>/dev/null
    exit $EXITCODE
fi
EOF

RUN chmod +x /app/start.sh

ENV DISPLAY=:0

CMD ["/app/start.sh", "-f"]
