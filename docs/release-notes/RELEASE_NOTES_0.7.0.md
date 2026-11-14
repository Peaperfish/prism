# Prism 0.7.0 – Real pixels

Zoom in past the preview and Prism shows the photo's own pixels. They are made from the full-size photo through your frame and every adjustment, so at 100% and above each one is the pixel an export at the photo's own size makes there. Sample colour and Pick neutral read the same pixels.

## New

- **Zoom to the photo's own pixels.** Past the preview's size, the canvas draws the photo at full size, rendered through the frame and every stage. At 100% and above, each pixel on screen is exactly the pixel an export at the photo's own size has there (before JPEG compression). Below 100% the pixels are shrunk to fit your screen.
- **A zoom you can read.** 100% is one pixel of the photo on each pixel of your screen, and you can zoom up to 800%. The `1` key goes to 100%, and double-click switches between Fit and 100%. On a large photo, Fit reads as a percentage of the photo too, for example 25%.
- **Sample colour reads real pixels.** At 100% and above, Sample colour reads the photo's own pixel under the cursor, and its block says "A real pixel: one pixel of the photo at its full size." Its colour values are those of the same pixel in an export at the photo's own size.
- **Pick neutral averages the same part of the photo at every zoom.** The picker averages a patch the size of 5 × 5 preview pixels, which is 19 × 19 photo pixels on a photo 9564 pixels wide. At 100% and above it averages the photo's own pixels, just as an export makes them. A small square follows the cursor to show the patch.
- **"· preview" on the zoom chip.** When the canvas shows the preview enlarged for a reason that lasts, the chip says so, for example "400% · preview". Pointing at it explains why: the full-size photo is still being read, it can't be read, or it would take more than a quarter of the computer's memory; the part on screen is too large to draw at full size; or the view is drawn from the preview, as on the easel, the test strip, in Compare, at an earlier stage, with the clipping marks or the stroke mask.
- **Reading the full-size photo.** After you open a photo larger than the preview, Prism reads it again at full size in the background: a second or two, and about 0.7 GB of memory for a 60 megapixel RAW file. Until it's ready you see the preview enlarged, never a blank. If the full-size photo would take more than a quarter of the computer's memory, the zoom shows the preview enlarged instead. An export started in the meantime pauses the read, and the read starts again afterwards.

## Changed

- **The zoom number is a percentage of the photo's own pixels.** The chip no longer adds "of preview" after the number.
- **"100% of Preview" is now "100% (real pixels)"** in the View menu.

## Known limits

- At Fit, a tight crop looks softer than it exports, because Fit is drawn from the preview. Zoom in to see the photo's own pixels.
- On a photo too large for the memory rule, zooming past the preview shows the preview enlarged, not the photo's own pixels.
- Frames and brush strokes aren't saved in recipes, and they are lost when the photo closes. Prism asks before closing, even straight after an export.
- The apps are not signed or notarized yet, so macOS and Windows warn the first time you open them. ([#11](https://github.com/Peaperfish/prism/issues/11))
- HEIC uses about 0.5 GB more memory than the same photo as a JPEG. ([#42](https://github.com/Peaperfish/prism/issues/42))

## Learn more

The recipe format is described in [docs/RECIPES.md](https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md), the app in the [user guide](https://github.com/Peaperfish/prism/blob/main/docs/GUIDE.md), and batches in the [command-line reference](https://github.com/Peaperfish/prism/blob/main/docs/CLI.md).
