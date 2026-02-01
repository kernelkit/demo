# Infix Demo Collection

Two demo applications for trade shows, exhibitions, and kiosk displays.

## Demos

### [Classic](classic/README.md) -- Oldschool Demoscene

A classic demoscene-style demo with multiple visual effects, text scrollers,
and tracker music.  Built with SDL2 and inspired by 1990s Amiga/PC demos.

### [Breeze](breeze/README.md) -- Weather & Time Display

A GTK-based weather and time display with animated backgrounds.  Touch
anywhere to temporarily show a configurable web page (e.g., a dashboard),
then automatically return to the weather view.

## Quick Start

```bash
# Build everything
make

# Or build individually
make -C classic
make -C breeze

# Run with Docker Compose
docker compose up classic
docker compose up breeze
```

## License

MIT License -- See [LICENSE](LICENSE) for details.
