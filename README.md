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
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libsdl2-mixer-dev
```

## Building

To build without music:

```
make
```

To build with music, place a `music.mod` file in the directory and run make:

```
make
```

The build system will automatically detect and embed the music file.

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

## Assets & Licenses

### Font

The FontStruction [Amiga Topaz 8][1] by [nonarkitten][2] is licensed
under a Creative Commons Attribution [Share Alike license][3].

[1]: https://fontstruct.com/fontstructions/show/889446
[2]: https://fontstruct.com/fontstructors/828567/nonarkiteen
[3]: http://creativecommons.org/licenses/by-sa/3.0/

### Music

The demo includes **"Enigma"** by [Phenomena][4], licensed under the
[Mod Archive Distribution license][5].

[4]: https://modarchive.org/index.php?request=view_artist_modules&query=72943
[5]: https://modarchive.org/index.php?request=view_by_license&query=publicdomain

## Adding Custom Music

The demo supports MOD/XM/IT tracker music files. To replace the music:

1. Download a compatible MOD file from [The Mod Archive][6]
2. Rename it to `music.mod`
3. Place it in the project directory
4. Run `make clean && make`

[6]: https://modarchive.org/
