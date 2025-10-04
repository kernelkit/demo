sdl_CFLAGS = $(shell pkg-config --cflags sdl2 SDL2_ttf SDL2_image SDL2_mixer)
sdl_LIBS   = $(shell pkg-config --libs sdl2 SDL2_ttf SDL2_image SDL2_mixer)

CFLAGS     =  $(sdl_CFLAGS) -Wall -Wextra -O2
LDLIBS     = $(sdl_LIBS) -lm
DEBUGFLAGS = -g -O0 -DDEBUG

TARGET     = demo
SOURCE     = demo.c
HEADERS    = font_data.h image_data.h

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
	rm -f $(TARGET) font_data.h image_data.h music_data.h
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
	@echo "Creating AppImage..."
	@mkdir -p AppDir/usr/bin
	@mkdir -p AppDir/usr/share/applications
	@mkdir -p AppDir/usr/share/icons/hicolor/256x256/apps
	@cp $(TARGET) AppDir/usr/bin/
	@printf '[Desktop Entry]\nType=Application\nName=Infix Demo\nExec=usr/bin/demo\nIcon=demo\nCategories=Game;\n' > AppDir/usr/share/applications/demo.desktop
	@cp jack.png AppDir/usr/share/icons/hicolor/256x256/apps/demo.png
	@ln -sf usr/share/applications/demo.desktop AppDir/demo.desktop
	@ln -sf usr/share/icons/hicolor/256x256/apps/demo.png AppDir/demo.png
	@ln -sf usr/bin/demo AppDir/AppRun
	@wget -q -c https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage -O appimagetool
	@chmod +x appimagetool
	@./appimagetool AppDir InfixDemo-x86_64.AppImage
	@rm -rf AppDir appimagetool
	@echo "AppImage created: InfixDemo-x86_64.AppImage"

.PHONY: all clean run debug appimage docker-build docker-run
