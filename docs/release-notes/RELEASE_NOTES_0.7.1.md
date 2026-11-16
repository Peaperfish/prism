# Prism 0.7.1 – Polish

Small fixes and polish after 0.7.0: keyboard shortcuts during a brush stroke and while framing, typed numbers and minus signs, the Pick neutral note, and the name of a look picked over a recipe.

## Changed

- **Typing a Kelvin value under 100 means thousands of kelvin, in the window's Kelvin field.** "5.2", "5.2k" and "5,2" are 5200 K, and "15" is 15000 K. `--kelvin` and recipe files still take whole kelvin, so `5.2` is refused there. ([#62](https://github.com/Peaperfish/prism/issues/62))
- **With Contrast switched off, its row names the grade it comes back to, without the multiplier**, for example "Grade 3 when on". ([#62](https://github.com/Peaperfish/prism/issues/62))
- **Negative numbers show a real minus sign (−) in more places**: in the messages about values out of range, on the command line ("--tint needs a number from −100 to 100, got '−101'") and in recipe files ("[white_balance] tint must be from −100 to 100 (it is −101)"), and in the key table in RECIPES.md. ([#76](https://github.com/Peaperfish/prism/issues/76))
- **Recipe files and the command line's number options read a real minus sign (−) as well as a hyphen.** Saved recipes still write a hyphen. ([#93](https://github.com/Peaperfish/prism/issues/93))
- **Saved recipes describe Kelvin the way `--help` does: "higher makes the picture warmer".** Recipes saved before still open as before. ([#62](https://github.com/Peaperfish/prism/issues/62))
- **A built-in look picked after loading a recipe takes the recipe's place.** The Look menu, the line under it and the Save Recipe name and description all name the look. Undo brings the recipe back. ([#35](https://github.com/Peaperfish/prism/issues/35))
- **The Pick neutral note goes after 6 seconds.** ([#62](https://github.com/Peaperfish/prism/issues/62))

## Fixed

- Dragging the panel's edge past its narrowest or widest and back, the edge stays under the mouse. ([#61](https://github.com/Peaperfish/prism/issues/61))
- Open (Cmd/Ctrl+O), Load Recipe (Cmd/Ctrl+Shift+O) and Export (Cmd/Ctrl+E or Cmd/Ctrl+S) do nothing while you are painting a brush stroke. ([#66](https://github.com/Peaperfish/prism/issues/66))
- While framing, with a control's keyboard highlight showing, Return keeps framing on. In 0.7.0 it always ended framing, even then. ([#74](https://github.com/Peaperfish/prism/issues/74))
- Recipe, colour matrix and camera profile numbers read the same whatever your computer's language settings. A number too big to hold is refused instead of read wrongly. ([#34](https://github.com/Peaperfish/prism/issues/34))

## Known limits

- At Fit, a tight crop looks softer than it exports, because Fit is drawn from the preview. Zoom in to see the photo's own pixels.
- On a photo too large for the memory rule, zooming past the preview shows the preview enlarged, not the photo's own pixels.
- Opening another photo while a stopped full-size read is finishing reads that photo twice. ([#82](https://github.com/Peaperfish/prism/issues/82))
- Brush size keys may not work on every keyboard layout. ([#64](https://github.com/Peaperfish/prism/issues/64))
- Frames and brush strokes aren't saved in recipes, and they are lost when the photo closes. Prism asks before closing, even straight after an export.
- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them. ([#11](https://github.com/Peaperfish/prism/issues/11))
- HEIC uses about 0.5 GB more memory than the same photo as a JPEG. ([#42](https://github.com/Peaperfish/prism/issues/42))

## Learn more

The recipe format is described in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), the app in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), and batches in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).
