#! /bin/sh

ARGS=-d # Start in daemon mode
case "$1" in
    start)
        echo "Starting aesd-gnssposget-server"
        start-stop-daemon -S -n aesd-gnssposget-server -a /usr/bin/aesd-gnssposget-server -- "${ARGS}"
        ;;
    stop)
        echo "Stopping aesd-gnssposget-server"
        start-stop-daemon -K -n aesd-gnssposget-server
        ;;
    *)
        echo "Usage: $0 {start|stop}"
    exit 1
esac

exit 0
