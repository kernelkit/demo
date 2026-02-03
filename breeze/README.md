# Breeze -- Weather & Time Display

A GTK-based weather and time display with animated Cairo backgrounds.
Touch anywhere to temporarily show a configurable web page (e.g., a
dashboard), then automatically return to the weather view after 30
seconds.

## Features

- Live weather from [Open-Meteo](https://open-meteo.com/) (no API key needed)
- Animated backgrounds: sky gradient, sun, clouds, rain, snow
- Sunrise/sunset times
- Location lookup by city name (geocoding via Open-Meteo)
- Touch/click to show a web page (WebKitGTK), auto-returns after 30s
- Fullscreen kiosk mode

## Quick Start

### Build and Run

```bash
sudo apt install libgtk-3-dev libwebkit2gtk-4.1-dev libsoup-3.0-dev libcjson-dev
make
./breeze -l Stockholm -f
```

### Run with Docker

```bash
docker compose up breeze
```

Or standalone:

```bash
docker run --rm -it \
  --privileged \
  -v /dev/fb0:/dev/fb0 \
  -v /dev/tty1:/dev/tty1 \
  -v /dev/input:/dev/input \
  -v /run/udev:/run/udev:ro \
  -e LOCATION=Stockholm \
  -e WEB_URL=https://example.com \
  ghcr.io/kernelkit/breeze:latest
```

## Command-Line Options

```
Usage: breeze [OPTIONS]

  -f, --fullscreen         Run in fullscreen mode
  -l, --location LOCATION  City or Country,City (e.g., "Stockholm"
                           or "Sweden,Stockholm"), geocoded via Open-Meteo
  --lat LATITUDE           Latitude for weather (default: 59.3293)
  --lon LONGITUDE          Longitude for weather (default: 18.0686)
  --url URL                Web page URL shown on touch/click
  -h, --help               Show this help message
```

Environment variables `LATITUDE`, `LONGITUDE`, `LOCATION`, and `WEB_URL`
are used as fallbacks when command-line options are not given.

Press Escape to exit.

## Dependencies

| Library        | Package (Debian/Ubuntu)    | Purpose              |
|----------------|----------------------------|----------------------|
| GTK 3          | `libgtk-3-dev`             | GUI framework        |
| WebKitGTK 4.1  | `libwebkit2gtk-4.1-dev`    | Embedded web view    |
| libsoup 3.0   | `libsoup-3.0-dev`          | HTTP client          |
| cJSON          | `libcjson-dev`             | JSON parsing         |

## Vendored Code

The following files are vendored (copied into this repository) to avoid
external build-time dependencies on libraries not commonly packaged:

| Files                    | Origin                                                                 | License      |
|--------------------------|------------------------------------------------------------------------|--------------|
| `sunriset.c`, `sunriset.h` | [troglobit/sun](https://github.com/troglobit/sun) by Paul Schlyter | Public domain |

Originally written as DAYLEN.C (1989) by Paul Schlyter, modified to
SUNRISET.C (1992), split into header by Joachim Nilsson (2017).
Released to the public domain by Paul Schlyter, December 1992.

## License

MIT License -- See [../LICENSE](../LICENSE) for details.
