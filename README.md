# Infix Demo

A classic demoscene-style demo featuring multiple visual effects with
oldschool aesthetics.

## Scenes

The demo cycles through different scenes.

**Starfield** — 3D scrolling stars with sine wave text scroller  
**Plasma** — Colorful plasma effect with sine wave text scroller  
**Rotating Cube** — Texture-mapped 3D cube with copper bars background and traditional bottom scroller  
**Tunnel** — Psychedelic tunnel effect with traditional bottom scroller

## Dependencies

On a Debian/Ubuntu/Mint system:

```
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev
```

## Building

```
make
```

## Running

```
make run
```

Or run a specific scene:

```
./demo 2  # Run only the cube scene
```

## Docker

```
make docker-run
```

## Font License

The FontStruction [Amiga Topaz 8][1] by [nonarkitten][2] is licensed
under a Creative Commons Attribution [Share Alike license][3].

[1]: https://fontstruct.com/fontstructions/show/889446
[2]: https://fontstruct.com/fontstructors/828567/nonarkitten
[3]: http://creativecommons.org/licenses/by-sa/3.0/
