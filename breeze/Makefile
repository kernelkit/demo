gtk_CFLAGS  = $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.1 libsoup-3.0 libcjson)
gtk_LIBS    = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.1 libsoup-3.0 libcjson)

CFLAGS     = $(gtk_CFLAGS) -DSUNRISET_LIB -Wall -O2
LDLIBS     = $(gtk_LIBS) -lm

TARGET     = breeze

# Derived, so that adding a source file is not three edits and a broken
# container build a week later
SOURCES    = $(wildcard *.c)
HEADERS    = $(wildcard *.h)

# sunriset.[ch] is vendored, it stays in its upstream formatting
VENDORED   = sunriset.c sunriset.h
FMT_FILES  = $(filter-out $(VENDORED),$(SOURCES) $(HEADERS))

all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

fmt:
	clang-format -i $(FMT_FILES)

clean:
	rm -f $(TARGET)

docker-build:
	docker build -t breeze .

docker-run: docker-build
	xhost +local:docker
	docker run -it --rm \
		-e DISPLAY=$(DISPLAY) \
		-v /tmp/.X11-unix:/tmp/.X11-unix \
		breeze

.PHONY: all clean fmt run docker-build docker-run
