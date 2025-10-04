# Infix Demo

A classic demoscene-style demo featuring multiple visual effects with
oldschool aesthetics.

## Scenes

The demo cycles through different scenes.

- **Starfield** — 3D scrolling stars with sine wave text scroller
- **Plasma** — Colorful plasma effect with sine wave text scroller
- **Rotating Cube** — Texture-mapped 3D cube with copper bars background
  and traditional bottom scroller
- **Tunnel** — Psychedelic tunnel effect with traditional bottom scroller
- **Bouncing Logo** — Animated logo with bouncing physics and rotation effects

## Dependencies

On a Debian/Ubuntu/Mint system:

```
sudo apt install libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libsdl2-mixer-dev
```

## Building

```
make
```

## Running

```
make run
```

### Command-Line Options

The demo supports various display and playback options:

```
./demo [OPTIONS] [SCENE...]

Display Options:
  -f, --fullscreen   Run in fullscreen mode (scales to display)
  -w, --window WxH   Set window size (e.g., 1920x1080)
  -s, --scale N      Integer scaling (e.g., 2 = 1600x1200)

Playback Options:
  -d, --duration SEC Scene duration in seconds (default: 15)
  -h, --help         Show help message

Scenes:
  0 - Starfield      3 - Tunnel           6 - 3D Star Ball
  1 - Plasma         4 - Bouncing Logo
  2 - Cube           5 - Raining Logo

Examples:
  ./demo -f              # Fullscreen, auto-cycle scenes
  ./demo -s 2            # 2x window size (1600x1200)
  ./demo -w 1920x1080    # Custom window size
  ./demo -d 30 2 6       # Show cube & star ball, 30s each
```

The demo renders internally at 800x600 and scales smoothly to any display
size while maintaining the 4:3 aspect ratio.

## Distribution

### AppImage (Recommended for sharing)

Create a portable AppImage that works on most Linux distributions:

```
make appimage
```

This creates `InfixDemo-x86_64.AppImage` - a single executable file you
can share with friends. They just need to:

1. Download the `.AppImage` file
2. Make it executable: `chmod +x InfixDemo-x86_64.AppImage`
3. Run it: `./InfixDemo-x86_64.AppImage`

**What is AppImage?**
- Single-file executable that works on most Linux distros
- No installation required - just download and run
- Bundles all dependencies (SDL2, fonts, music, textures)
- Works on Ubuntu, Debian, Fedora, Arch, etc.
- Great for distributing demos to friends!

### Docker

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
