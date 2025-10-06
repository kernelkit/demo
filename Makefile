sdl_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image SDL2_mixer)
sdl_LIBS   = $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image SDL2_mixer)

CFLAGS     =  $(sdl_CFLAGS) -Wall -Wextra -O2
LDLIBS     = $(sdl_LIBS) -lm
DEBUGFLAGS = -g -O0 -DDEBUG

TARGET     = demo
SOURCE     = demo.c
HEADERS    = font_data.h image_data.h logo_data.h infix_data.h wires_data.h

# Check if music file exists and add to build
ifneq ($(wildcard music.mod),)
HEADERS    += music_data.h
CFLAGS     += -DHAVE_MUSIC
endif

all: $(TARGET)

# Generate embedded font data from topaz-8.otf
font_data.h: topaz-8.otf
	xxd -i topaz-8.otf > font_data.h

# Generate embedded image data from jack.png
image_data.h: jack.png
	xxd -i jack.png > image_data.h

# Generate embedded logo data from logo.png
logo_data.h: logo.png
	xxd -i logo.png > logo_data.h

# Generate embedded infix data from infix.png
infix_data.h: infix.png
	xxd -i infix.png > infix_data.h

# Generate embedded wires data from wires.png
wires_data.h: wires.png
	xxd -i wires.png > wires_data.h

# Generate embedded music data from music.mod (if present)
music_data.h: music.mod
	xxd -i music.mod > music_data.h

$(TARGET): $(SOURCE) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE) $(LDLIBS)

debug: $(SOURCE) $(HEADERS)
	$(CC) $(CFLAGS) $(DEBUGFLAGS) -o $(TARGET) $(SOURCE) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) font_data.h image_data.h logo_data.h infix_data.h wires_data.h music_data.h
	rm -rf AppDir appimagetool InfixDemo-x86_64.AppImage

docker-build:
	docker build -t demo .

docker-run: docker-build
	xhost +local:docker
	docker run -it --rm \
		-e DISPLAY=$(DISPLAY) \
		-v /tmp/.X11-unix:/tmp/.X11-unix \
		demo

appimage: $(TARGET)
	@ARCH=$${ARCH:-x86_64}; \
	./utils/build-appimage.sh $${ARCH}

.PHONY: all clean run debug appimage docker-build docker-run
