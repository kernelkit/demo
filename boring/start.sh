#!/bin/sh
# Smart startup: use existing X or start our own

export XDG_CACHE_HOME=/tmp/.cache

cleanup()
{
    echo "Shutting down..."
    if [ -n "$APPPID" ]; then
        kill $APPPID 2>/dev/null
        wait $APPPID 2>/dev/null
    fi
    if [ -n "$XPID" ]; then
        kill $XPID 2>/dev/null
        wait $XPID 2>/dev/null
    fi
    exit 0
}

trap cleanup TERM INT

# Check if X server is already available
if xdpyinfo -display "${DISPLAY:-:0}" >/dev/null 2>&1; then
    echo "Using existing X server on $DISPLAY"
    exec dbus-launch ./boring "$@"
else
    echo "No X server found, starting embedded X server..."
    Xorg -noreset +extension GLX +extension RANDR +extension RENDER \
	 -logfile /tmp/xorg.log -config /etc/X11/xorg.conf :0 &
    XPID=$!
    sleep 2

    # Apply display rotation if requested
    if [ -n "$DISPLAY_ROTATE" ]; then
        echo "Applying display rotation: $DISPLAY_ROTATE"
        OUTPUT=$(DISPLAY=:0 xrandr | grep " connected" | head -1 | cut -d' ' -f1)
        DISPLAY=:0 xrandr --output "$OUTPUT" --rotate "$DISPLAY_ROTATE"
    fi

    # WebKitGTK requires a D-Bus session bus
    DISPLAY=:0 dbus-launch ./boring "$@" &
    APPPID=$!

    wait $APPPID
    EXITCODE=$?

    kill $XPID 2>/dev/null
    wait $XPID 2>/dev/null
    exit $EXITCODE
fi
