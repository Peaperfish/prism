# Prism 0.6.0 – Crop & straighten

This release adds the easel. It frames and straightens the photo before any stage runs: the crop, the turn and the shape are chosen once, and every stage after them works on the framed print. The same frame is available on the command line.

## New

- **Frame and straighten on the easel.** Press `C`, choose View → Frame, or click the Easel line at the top of the Light path. Eight handles move the blades: drag a corner to move it while the opposite corner stays put, drag an edge to move that one blade, and drag inside the frame to slide it over the photo. The Aspect menu locks the shape to Original, Free, 1:1, 4:5, 5:7, 3:2 or 16:9, and `X` turns it between landscape and portrait.
- **Straightening.** The Straighten slider turns the photo from −45° to +45° in steps of 0.1°, and its readout names the direction, for example "1.2° clockwise". Hold Option (Alt on Windows and Linux) and drag a line along a horizon or an upright in the photo, and on release the photo turns to level it. Ctrl-click (Cmd-click on a Mac) the slider to type an angle.
- **Guides while you work.** Rule-of-thirds lines show while you drag the blades, and a finer grid shows while you straighten or drag a horizon. They disappear when you let go.
- **The corners never show.** When a turn or a blade would run past the photo's edge, the blades close in, keeping their shape, and a note at the foot of the canvas says so.
- **Undo, and keeping the frame.** Return, Done, `C` or Esc leave framing and keep the frame. Esc during a drag cancels only that drag. Each handle drag, slider drag, typed angle, aspect choice, turn and Reset is one undo step.
- **Export at the frame's full size.** Original, Long side and Percent all count from the frame's full resolution. The Export sheet shows it, for example "Full size of the frame, 5120 × 3413".
- **Frame on the command line.** `--rotate` turns the photo by a number of degrees from −45 to 45, `--crop l,t,r,b` keeps a box given as four fractions from 0 to 1, and `--aspect` makes the largest frame of one shape (`original`, `free`, `1:1`, `4:5`, `5:7`, `3:2`, `16:9`, and the turned forms such as `5:4` and `9:16`). They run first, before `--long-side`, and work with `--recipe`.

## Changed

- The easel is not a stage. It moves no light, so the scopes, histogram, clipping, Sample, Pick neutral, the test strip and Compare all measure only the framed photo. While you are framing, the canvas shows the whole photo, the scopes say so, and Compare is off.
- The Save Recipe sheet says "Framing isn't included." when the photo has a frame, so a frame is never saved in a recipe.

## Known limits

- A tight crop looks softer on screen than it exports, because the preview is made at most 2560 pixels on its longer side before framing. Zooming to the file's real pixels is tracked in [#37](https://github.com/Peaperfish/prism/issues/37).
- Frames and brush strokes aren't saved in recipes, and they are lost when the photo closes. Prism asks before closing, even straight after an export.
- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them. ([#11](https://github.com/Peaperfish/prism/issues/11))
- HEIC uses about 0.5 GB more memory than the same photo as a JPEG. ([#42](https://github.com/Peaperfish/prism/issues/42))

## Learn more

The recipe format is described in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), the app in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), and batches in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).
