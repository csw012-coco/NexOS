DOOM WAD files are not distributed with NexOS.

To run Doom inside NexOS, provide your own legally obtained WAD files in this
directory before building an image. Common filenames recognized by the Makefile:

- DOOM1.WAD
- DOOM2.WAD
- TNT.WAD
- PLUTONIA.WAD

The build will copy any files that exist here into `/home/doom` in the root
filesystem image. Missing files are skipped.
