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

rotate()
{
    [ -n "$DISPLAY_ROTATE" ] || return 0

    echo "Applying display rotation: $DISPLAY_ROTATE"
    OUTPUT=$(xrandr | grep " connected" | head -1 | cut -d' ' -f1)
    [ -n "$OUTPUT" ] || return 0
    xrandr --output "$OUTPUT" --rotate "$DISPLAY_ROTATE"

    # xrandr leaves touch coordinates unrotated, that needs a matrix
    case "$DISPLAY_ROTATE" in
	left)     MATRIX="0 -1 1 1 0 0 0 0 1" ;;
	right)    MATRIX="0 1 0 -1 0 1 0 0 1" ;;
	inverted) MATRIX="-1 0 1 0 -1 1 0 0 1" ;;
	*)        MATRIX="1 0 0 0 1 0 0 0 1"  ;;
    esac

    # Only touch devices carry a libinput calibration matrix
    xinput --list --name-only | while read -r DEV; do
	xinput list-props "$DEV" 2>/dev/null |
	    grep -q "libinput Calibration Matrix" || continue
	echo "Rotating touch input on \"$DEV\""
	xinput set-prop "$DEV" "Coordinate Transformation Matrix" $MATRIX
    done
}

# Check if X server is already available
if xdpyinfo -display "${DISPLAY:-:0}" >/dev/null 2>&1; then
    export DISPLAY="${DISPLAY:-:0}"
    echo "Using existing X server on $DISPLAY"
    rotate
    exec dbus-launch ./breeze "$@"
else
    echo "No X server found, starting embedded X server..."
    Xorg -noreset +extension GLX +extension RANDR +extension RENDER \
	 -logfile /tmp/xorg.log -config /etc/X11/xorg.conf :0 &
    XPID=$!
    sleep 2

    export DISPLAY=:0
    rotate

    # WebKitGTK requires a D-Bus session bus
    dbus-launch ./breeze "$@" &
    APPPID=$!

    wait $APPPID
    EXITCODE=$?

    kill $XPID 2>/dev/null
    wait $XPID 2>/dev/null
    exit $EXITCODE
fi
