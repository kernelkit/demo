Contributing Guidelines
=======================

Thank :heart: you for taking the time to read this!

Bug reports, fixes, and new effects are all welcome.  We *prefer* GitHub
pull requests, but are open to other forms of collaboration as well.

&nbsp;&nbsp; :bug: <https://github.com/kernelkit/demo/issues>  
&nbsp;&nbsp; :speech_balloon: <https://github.com/orgs/kernelkit/discussions>

:building_construction: Building
--------------------------------

Both demos build from the top directory, or on their own:

```bash
make                  # both
make -C classic       # demoscene demo, SDL2
make -C breeze        # weather display, GTK3 + WebKitGTK
```

The dependencies of each are listed in its own `README.md`.  There is no
configure step, and no generated files to check in.

Breeze is easier to work on with the condition forced, so you do not
have to wait for the weather to oblige:

```bash
./breeze --weather thunder
```

:art: Style
-----------

Match the file you are editing.  The two demos do not agree, and that is
fine -- each is internally consistent:

- `classic/` follows Linux kernel style, tabs at eight columns
- `breeze/` is indented with four spaces

`breeze/sunriset.[ch]` is vendored from [troglobit/sun][sun].  Fixes
belong upstream first, then come back here as a re-import, so please do
not reformat or refactor those two files.

:memo: Commits
--------------

Write the problem, then the cause, then the fix, and stop there.  The
reader has the diff, so it does not need narrating.  Keep one concern
per commit rather than one large one, and sign off your work:

```bash
git commit -s
```

:framed_picture: Screenshots
----------------------------

If a change alters what breeze looks like, update the screenshots in
`breeze/screenshots/` so the README keeps telling the truth.  They are
1024x600, with 400px wide thumbnails, quantised to 256 colours to keep
the repository small.

[sun]: https://github.com/troglobit/sun
