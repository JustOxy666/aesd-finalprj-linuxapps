DBGFLAGS ?= -g -Wall
DBGBUILDFLAGS ?= -DDEBUG_ON
LDFLAGS ?=-lpthread
#CFLAGS += -I../aesd-char-driver
SRC ?= main.c gnssposget-server.c socket_connections.c accelmeter-app.c
OBJ ?= aesd-gnssposget-server

all:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(LDFLAGS) ${CFLAGS} $(DUSE_AESD_CHAR_DEVICE) -o $(OBJ) $(SRC)
debug:
	$(CROSS_COMPILE) $(CC) $(DBGFLAGS) $(LDFLAGS) ${CFLAGS} $(DBGBUILDFLAGS) $(DUSE_AESD_CHAR_DEVICE) -o $(OBJ) $(SRC)

clean:
	rm -f *.o aesd-gnssposget-server
