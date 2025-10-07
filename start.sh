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
    Xorg -noreset +extension GLX +extension RANDR +extension RENDER \
	 -logfile /tmp/xorg.log -config /etc/X11/xorg.conf :0 &
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
