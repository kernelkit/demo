gtk_CFLAGS  = $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 libsoup-3.0 libcjson)
gtk_LIBS    = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.1 libsoup-3.0 libcjson)

CFLAGS     = $(gtk_CFLAGS) -DSUNRISET_LIB -Wall -O2
LDLIBS     = $(gtk_LIBS) -lm

TARGET     = boring
SOURCES    = boring.c weather.c animations.c sunriset.c

all: $(TARGET)

$(TARGET): $(SOURCES) weather.h animations.h sunriset.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

docker-build:
	docker build -t demo-boring .

docker-run: docker-build
	xhost +local:docker
	docker run -it --rm \
		-e DISPLAY=$(DISPLAY) \
		-v /tmp/.X11-unix:/tmp/.X11-unix \
		demo-boring

.PHONY: all clean run docker-build docker-run
