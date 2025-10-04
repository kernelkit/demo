sdl_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image)
sdl_LIBS   = $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image)

CFLAGS     =  $(sdl_CFLAGS) -Wall -Wextra -O2
LDLIBS     = $(sdl_LIBS) -lm
DEBUGFLAGS = -g -O0 -DDEBUG

TARGET     = demo
SOURCE     = demo.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDLIBS)

debug: $(SOURCE)
	$(CC) $(CFLAGS) $(DEBUGFLAGS) -o $(TARGET) $(SOURCE) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

docker-build:
	docker build -t demo .

docker-run: docker-build
	xhost +local:docker
	docker run -it --rm \
		-e DISPLAY=$(DISPLAY) \
		-v /tmp/.X11-unix:/tmp/.X11-unix \
		demo

.PHONY: all clean run debug docker-build docker-run
