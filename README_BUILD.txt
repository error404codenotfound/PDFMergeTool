================================================================
  Quick PDF Tool — Build Instructions
================================================================

REQUIREMENTS
------------
1. MinGW-w64 (GCC for Windows)  — includes windres
   Download: https://github.com/niXman/mingw-builds-binaries/releases
   Recommended: x86_64-14.x.x-release-win32-seh-msvcrt

2. miniz  (ZIP library — single file)
   Download the release ZIP from:
   https://github.com/richgel999/miniz/releases
   You need TWO files from it:  miniz.h  and  miniz.c
   Place both in the same folder as PDFMergeTool.c

3. stb_image.h  (PNG loader — single header)
   Download raw file from:
   https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
   Place in the same folder as PDFMergeTool.c

4. ENERFLEX.png  — company logo (shown on the Merge screen)
   Place in the same folder as PDFMergeTool.c  (needed at BUILD time only).

5. efxicon.ico  — app icon (title bar, taskbar, and EXE file in Explorer)
   Place in the same folder as PDFMergeTool.c  (needed at BUILD time only).

Both asset files are compiled directly into the EXE — they do NOT need
to be shipped with or placed next to the final executable.


FOLDER STRUCTURE (before compiling)
-------------------------------------
PDFMergeTool\
  PDFMergeTool.c     <- main source (GUI)
  pdf_merge.c        <- PDF engine
  pdf_merge.h        <- PDF engine header
  resources.rc       <- embeds ENERFLEX.png + efxicon.png into the EXE
  miniz.c            <- download (step 2)
  miniz.h            <- download (step 2)
  stb_image.h        <- download (step 3)
  ENERFLEX.png       <- logo   (needed at compile time only)
  efxicon.ico        <- icon   (needed at compile time only)


COMPILE  (run from a Command Prompt with MinGW in PATH)
-------------------------------------------------------
Step 1 — compile the resource file (embeds the PNGs):

  windres resources.rc -o resources.o

Step 2 — compile and link everything:

  gcc PDFMergeTool.c pdf_merge.c miniz.c resources.o ^
      -o PDFMergeTool.exe ^
      -lcomctl32 -lgdi32 -lcomdlg32 -lshell32 -lshlwapi -lole32 ^
      -ldwmapi -ladvapi32 ^
      -mwindows -static-libgcc -O2

Or as a single command:

  windres resources.rc -o resources.o && gcc PDFMergeTool.c pdf_merge.c miniz.c resources.o -o PDFMergeTool.exe -lcomctl32 -lgdi32 -lcomdlg32 -lshell32 -lshlwapi -lole32 -ldwmapi -ladvapi32 -mwindows -static-libgcc -O2

This produces a single standalone EXE with all assets built in.
No installation or admin rights are required to run it.


DEPLOY
------
Copy ONLY the EXE to any Windows machine — no other files needed:
  PDFMergeTool.exe


FEATURES
--------
  [Merge PDF]       — Drop a ZIP file onto the tool.
                      It extracts all PDFs, sorts them A→Z,
                      and merges them into one output PDF.
                      Options: delete source ZIP, open output folder.

  [Split PDF]       — Drop a single PDF.
                      Each page is saved as page_001.pdf, page_002.pdf, ...

  [Arrange & Merge] — Add individual PDFs, reorder with Move Up/Down,
                      remove unwanted files, then merge in your chosen order.

  [Coming Soon]     — Placeholder for a future feature.


NOTES
-----
- The PDF engine handles standard PDFs (unencrypted) including both
  classic xref tables (PDF 1.4 and earlier) and compressed xref streams
  (PDF 1.5+, used by Microsoft Office, Adobe Acrobat, and most modern tools).
- Very large PDFs (>500 MB total) may be slow due to in-memory processing.
- windres is included with MinGW-w64; no separate download is needed.

================================================================
