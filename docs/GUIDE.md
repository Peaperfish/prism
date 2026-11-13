# Prism user guide

Prism is a photo editor for PNG, JPEG and camera RAW files. It shows what each step of an edit does to the light, so you can see why a photo looks the way it does. This guide covers the desktop app. The command-line tool is in [CLI.md](CLI.md).

![The Prism window: a photo with the control panel beside it, in the dark theme](images/prism-window.jpg)

Contents: [First launch](#first-launch) · [A walkthrough](#a-walkthrough) · [Recipes](#recipes) · [Keyboard shortcuts](#keyboard-shortcuts) · [How Prism thinks about light](#how-prism-thinks-about-light) · [Preferences](#preferences) · [Reference details](#reference-details)

## First launch

Download the zip for your system from the [Releases](https://github.com/Peaperfish/prism/releases) page:

| Download | For |
|---|---|
| `prism-<version>-macos-universal.zip` | macOS 13.3 or later, Apple Silicon and Intel |
| `prism-<version>-windows-x64.zip` | Windows 10 and 11, 64-bit |

Each zip holds the app (`Prism.app` on macOS, `prism_gui.exe` on Windows), the command-line tool (`prism`, `prism.exe`), Prism's `LICENSE.md`, `THIRD_PARTY_NOTICES.txt`, LibRaw's and Inter's licence files in `licenses/LibRaw/` and `licenses/Inter/`, and a short `README.txt`. Both include RAW support. Nothing needs installing beside them.

Each release also has `LibRaw-<version>-source.tar.gz`, the LibRaw source the builds were made from, and `SHA256SUMS.txt`, the checksums of the downloads.

### The unsigned-app warning

Prism isn't signed by Apple or Microsoft yet, so both systems warn you the first time you open it. The app is the same either way; the warning is about the missing signature.

**macOS.** Unzip, and drag `Prism.app` to Applications.

1. Right-click (or Control-click) `Prism.app` and choose **Open**, then **Open** again.
2. On macOS 15 and later that no longer works. Try to open Prism once, then go to **System Settings → Privacy & Security**, scroll down to the message about Prism and click **Open Anyway**.

After that it opens normally. Only downloaded files are checked: a build you made yourself opens straight away.

**Windows.** Extract the whole zip (don't run the app from inside the zip), then run `prism_gui.exe`.

1. If Microsoft Defender SmartScreen says "Windows protected your PC", click **More info → Run anyway**.
2. If Smart App Control (Windows 11) is on, it blocks unsigned apps and there is no way to allow just one. Prism won't run until Smart App Control is off.

## A walkthrough

This follows one photo from opening to export. The photo is on the left of the window and the edit panel on the right, with the menu bar above and the status bar below. The panel runs from top to bottom: the header (Undo, Redo and Export…), **Scopes**, **Look**, the **Light path**, and, while you are reading a pixel, the **Sample** block. The **Explain area** is fixed at the foot of the panel. Hover over any control: the Explain area says what it does to the light.

### Open a photo

- **File → Open…** (`Cmd+O` on macOS, `Ctrl+O` on Windows and Linux), or the **Open photo…** button on the empty window.
- **Drag and drop** a file onto the window, also when a photo is already open. Prism opens one photo at a time: if you drop several, it opens the first and says so.
- Or give the path when starting the app (`prism_gui photo.jpg`), or use "Open with" on Windows.

Prism opens PNG, JPEG, HEIC/HEIF (macOS only) and RAW files (`.arw`, `.dng`, `.cr2`, `.cr3`, `.nef`, `.raf`). The preview is shrunk to fit 2560 pixels on its longer side, so editing stays quick. For a RAW file the preview is also decoded at half size; the status bar's tooltip says "RAW, half-size preview". Export always starts again from the full-size file.

A JPEG opens upright: Prism turns it as its camera's orientation setting (EXIF) says, in the preview and in an export, so a photo taken with the camera on its side is not shown sideways. A photo with an unreadable orientation opens as it is stored.

HEIC photos (what an iPhone takes) open on macOS only; Windows and Linux say so instead. Apple's image framework decodes them, upright, and the colours are converted from the photo's own colour space (usually Display P3) to Prism's, so very saturated colours outside sRGB are kept rather than clipped at once. Only the standard picture is read: the extra HDR layer an iPhone adds is ignored.

The [status bar](#the-status-bar) at the foot of the window says what was opened. If a file can't be opened, a message appears over the photo with a **Details** section and a **Copy** button; the photo you had open stays open.

### The header

At the top of the panel are **Undo** and **Redo**, two icon buttons, and **Export…**, the only coloured button. A button that can't act right now is faint. Point at Undo or Redo for its key (`Cmd+Z`, and `Shift+Cmd+Z` for Redo on a Mac). See [Undo and redo](#undo-and-redo) and [Export](#export).

### The Look menu

**Look** is a menu of starting points, one full-width button under the section's name:

- RAW files: Natural, Original, Vivid, Soft, Black & white.
- PNG and JPEG files: Original, Vivid, Soft, Black & white.

A look is not hidden. It sets four things you can see in the [Light path](#the-light-path) below: Exposure, Saturation, Contrast and the Tone curve. A RAW file opens with Natural (+1.00 stops and the filmic curve), because a RAW file is linear and looks dark and flat as it comes from the sensor. A PNG or JPEG opens with Original, which changes nothing. Point at the menu or at a look and the Explain area names its numbers, for example "Vivid: +1.00 stops, colour ×1.25, contrast ×1.15, filmic".

The button shows what the settings are now:

- the name of the look, while the numbers match one;
- **Custom**, once you change any of them so that no look matches (with a recipe loaded, it reads **Recipe:** *name* **(modified)** instead);
- **Recipe:** *name*, while a [recipe](#recipes) you loaded is in force. The name reads *name* (modified) once you change something.

At the right of the section's header, **Reset to Natural** (for a PNG or JPEG, **Reset to Original**) goes back to the starting look. It is one undo step, and it is hidden while the settings already are that look.

Below the first group in the list, after a separator, are three actions:

- **View recipe…** opens the look as the [recipe](#recipes) it is: the file built into Prism, as text you can select, with a **Copy** button. It is greyed out while no built-in look matches (**Custom**). The window keeps showing the look you opened it for, even if you then change the settings, and it does not stop you editing the photo. A built-in look leaves the colour matrix and the RAW options alone, as the window says.
- **Load recipe…** and **Save recipe…** are the same as the File menu's. They are greyed out while a file dialog or the save sheet is open, an export is running, or no photo is open.

While a recipe is loaded, a muted line under the button says what it set: "Sets all five stages." or, for a recipe with only some of them, "Sets only Colour matrix, Saturation and Contrast; the other stages are unchanged." Point at it for the recipe's description, if it has one.

### Scopes

The **Scopes** section shows how the light is spread over the stage you are viewing. Four tabs choose the scope; Prism remembers your choice.

| Tab | What it shows |
|---|---|
| **Histogram** | How many pixels sit at each level, black on the left and white on the right. Luminance is a grey fill behind; red, green and blue are translucent fills that add towards white where they overlap. |
| **Parade** | The level of each channel, in red, green and blue panels side by side, left to right across the photo. Equal heights mean neutral. |
| **Waveform** | The level of each part of the photo: it sits where the photo does from left to right, and the height is the luma, black at the bottom and white at the top. A stronger trace means more pixels there. |
| **Vector** | The vectorscope: which colours are present and how strong. Angle is hue and distance from the centre is strength; the centre is neutral grey, and each dot is drawn in its own hue. The plot is round with two faint rings, and every colour lands inside the outer one. |

The plot is dark in both themes, and every scope is the same height, so nothing below it moves when you switch.

**Guides.** The Parade and the Waveform have a line at each of five levels: white, 50%, 18%, 5% and black, labelled on the plot. They are shares of white's light, placed where the 8-bit picture shows them; 18% is middle grey. The vectorscope has targets for red, magenta, blue, cyan, green and yellow (R, Mg, B, Cy, G, Yl) at 75% strength, and a line where skin tones usually sit; point at it for a note. The scopes measure the 8-bit picture on screen, not the linear values.

**The clip triangles.** In the top corners of the Histogram's plot are two small triangles: the left one for the shadows and the right one for the highlights. A triangle lights up (blue on the left, orange on the right) when any pixel is out of range at that end, and is faint otherwise.

**The readout line.** Under the plot, the left end says what share of pixels are below black (a blue dot) and the right end what share are above white (an orange dot), for example "0.1% below black" and "0.0% above white". A pixel counts as **below black** when it has no light left (it is black or darker because a channel is below 0), and as **above white** when any of its channels is above 1, at the stage you are viewing. A pixel that still has light but a channel below 0 is a colour outside sRGB: it is not in "below black", and the tooltip gives its share in its own line. Numbers round to a tenth of a percent, with "<0.1%" for less. The numbers are the same for every scope, and match the histogram. Point at the line for the tooltip: it holds the sentence that says where clipping starts and where it costs detail, for example "Highlights first clip at Exposure (2.1% of pixels); still recoverable until Output encode." or "Nothing clips." Point at either end for the share of red, green and blue on their own (counted by the same rules), with a reminder that `J` also marks pixels an earlier stage cut off, so it can show marks while the readout reads 0.0%.

**Marking clipping on the photo.** The action at the right of the Scopes header, which ends in `(J)` and reads Show or Hide, or the `J` key, marks those pixels on the preview for the stage you are viewing: orange-red for brighter than white, blue for no light left, and violet diagonal stripes (about 4 preview pixels wide, so the colour underneath stays visible, and wider on screen as you zoom in) for too saturated for sRGB. A pixel that an earlier stage already cut off stays marked in later stages. It is off by default, and Prism remembers the setting. While you view an earlier stage or a test strip band, the title says which, for example "Scopes (Exposure)" or "Scopes (−½ stop band)"; while you are framing on the easel, the scopes measure the whole photo and the title says so ("Scopes (whole photo, while framing)").

### The Light path

The **Light path** lists the stages the light goes through, from the file to the screen, one row each, in order:

1. **Decode**: the file turned into linear light.
2. **Exposure**
3. **Dodge & burn**: strokes you paint to hold back or add light in parts of the photo (see [Dodge and burn with the Brush](#dodge-and-burn-with-the-brush)).
4. **White balance**
5. **Colour matrix**
6. **Saturation**
7. **Contrast**
8. **Tone curve**
9. **Output encode**: light turned into the 8-bit sRGB file.

(The section [How Prism thinks about light](#how-prism-thinks-about-light) explains why the order matters.) Above Decode sits the **Easel** line, with its **Frame** button, which frames and straightens the print before the stages run: it has no dot on the rail, no switch and no clip marks, because it moves no light (see [Frame and straighten on the easel](#frame-and-straighten-on-the-easel)). The stages are the controls: there are no separate sliders. Each photo opens at Decode, the first row. A RAW looks dark and flat there, since nothing has been done to it yet, and a note at the top of the photo says so: "Showing Decode — the file's light before any edit. Pick a later stage to see more." The note goes when you view a later stage, and while Compare is on.

A thin rail runs down the left, with a dot for each stage. The **viewed stage**'s dot is filled with the accent colour and has a faint glow, and its row has a lighter wash behind it. The photo, the scopes and the Sample block show the light at that stage. Output encode is the final picture.

**Viewing a stage.** Click anywhere on a row (or its dot) to see the photo as it looks right after that stage. A badge over the photo says **Viewing: after** *stage* and has a **Final image (Esc)** button. Pressing `Esc` or clicking the Output encode row brings back the final picture.

**What a row holds.** The first line has the stage's name, its value and its switch. Under it:

| Row | Value | Under the name |
|---|---|---|
| Decode | none | what the file is: "RAW, camera colour", "RAW, your camera matrix", "JPEG, read as sRGB", "PNG, read as sRGB" or "HEIC, converted from its own colours" |
| Exposure | `+1.00 stops` | a slider from −2 to +2 stops, and the **Test strip** button |
| Dodge & burn | `None`, or the strokes: `2 dodges`, `1 burn`, `3 strokes` | the **Brush** button, the **Strength** slider and **Clear** (see [Dodge and burn with the Brush](#dodge-and-burn-with-the-brush)) |
| White balance | `As shot`, or `Chosen` once you have chosen a light | "The light the photo was lit by" (or "Light not recorded, taken as 6500 K"), the **Pick neutral** button, and two sliders, each with its number beside it: **Kelvin** (`5810 K`) and **Tint** (`+34`) (see [White balance](#white-balance)) |
| Colour matrix | the matrix: "Camera default" (RAW) or "None", the file's name, or "From recipe" | nothing else: the app gets a matrix only from a [recipe](#recipes) |
| Saturation | `×1.00` (`black and white` at exactly 0) | a slider from `×0.00` to `×2.00` |
| Contrast | `Grade 2 · ×1.00` | a slider in half grades, from grade 0 to grade 5 (see [Contrast as a paper grade](#contrast-as-a-paper-grade)) |
| Tone curve | none | four tabs: **None**, **Gamma**, **Reinhard**, **Filmic** |
| Output encode | none | "sRGB for the screen" |

The sliders of Exposure, Saturation and Contrast are centred on no change. Hover over a row for a one-sentence explanation; the Explain area says more. Point at the Colour matrix's value for the full path of its file.

**Switches.** Exposure, Dodge & burn, White balance, Colour matrix, Saturation, Contrast and Tone curve each have a switch. Off, the photo shows what it looks like without that stage. The stage keeps its value, so switching it on again brings it back, and while it is off the row says what it will be: "+0.70 stops when on". The stage's slider and tabs are greyed out meanwhile. Each switch is one undo step. Decode and Output encode have none.

**The value line and dragging.** Drag a slider and the photo follows. A whole drag is one undo step. If a value is beyond what the slider can show (from a recipe, say), the row says **(beyond the slider)** (Contrast does not: its value already reads "Softer than grade 0" or "Harder than grade 5"), and the slider sits at its end until you move it. While a stage is off its slider sits at neutral, so nothing is beyond it.

<a id="white-balance"></a>
**White balance.** White balance tells Prism what light the photo was lit by, so that something white in the scene comes out white. You name the light with two numbers:

- **Kelvin** is the colour of the light, from 2000 K (candlelight, very orange) to 15000 K (deep shade, very blue). Daylight is around 5500 K. The number describes the light, not the picture: a higher Kelvin says the light was bluer, so Prism warms the photo to make up for it, and a lower Kelvin cools it.
- **Tint** runs from −100 to +100, across the other direction, from green to magenta. It also describes the light: towards the right end you say the light was greener, so the photo turns more magenta, and towards the left end the photo turns greener. 0 is a light with no green or magenta cast.

Each slider is a line of its own: its name, the slider, and its number at the right (`Kelvin [slider] 5810 K`, `Tint [slider] +34`, with a real minus sign for a negative tint and a plain `0` for none). The numbers share a column, so the two sliders have the same length and end in the same place.

Until you choose a light the row reads **As shot**: the light the camera balanced for when it took the photo, which leaves the photo exactly as it was decoded. For a RAW file both sliders sit at that light and their numbers read it, for example `5330 K` and `+27`. A JPEG, PNG or HEIC does not say what light it was balanced for (and a few RAW files don't either), so it is **taken as 6500 K**, tint 0: the line under the name reads "Light not recorded, taken as 6500 K", and the numbers you choose are measured from there.

Move either slider and the row reads **Chosen** instead, and the numbers are your choice, for example `5200 K` and `+10`. The other number starts from the as-shot light. Kelvin moves in steps of 10 K and Tint in whole numbers. The Kelvin slider is even in mireds (a million divided by the Kelvin) rather than in Kelvin, so the same distance along it is about the same change in colour everywhere: its middle is about 3530 K, not 8500 K. The coloured part of each slider runs from the as-shot light to your choice. Right-click either slider for **Reset to As shot**, which removes the choice entirely. Switched off, the row keeps your choice and shows the photo as shot.

**Picking a neutral.** If something in the photo should be grey or white (a grey card, a white wall, a concrete pavement), let Prism find the light from it. Press `W`, choose **View → Pick Neutral**, or press the small **Pick neutral** button on the White balance row (it looks pressed while the mode is on), then click that spot. Prism averages the 5 by 5 pixels around the click (fewer at the picture's edge): pixels of the preview, or, zoomed in to real pixels at 100% or more, pixels of the photo itself, a smaller patch. It sets Kelvin and Tint so that colour comes out grey: the row then reads **Chosen**, with `5180 K` and `+4` beside the sliders, the stage is switched on, and a note over the photo says `Neutral here: 5180 K, tint +4`. A small square follows the cursor to show the patch that would be averaged. Each click is one undo step, and you can click again without leaving the mode: it always looks at the photo as decoded, before any white balance, so picking the same spot twice gives the same answer. Kelvin and Tint are rounded the way the sliders round, and a light beyond the sliders is set to the nearest end and the note adds "(limited to the slider's range)".

Prism refuses, and changes nothing, when the spot cannot tell it the light: **"Too dark to judge; click a lighter grey"** when any colour of the patch averages below 0.5% of white, and **"Clipped; click a grey that isn't blown out"** when any pixel in the patch is at white in the file or above it, since a clipped pixel has lost its colour. Dragging still moves the picture (and `Space` still holds the hand); a double-click still zooms; a click on a control does not pick. The picker needs White balance in view, so it is greyed out while you are viewing Decode, Exposure or Dodge & burn (a tooltip says why), and turning it on leaves framing on the easel, the Brush, Sample mode and the test strip. `Esc`, `W` again, `C`, `B`, `I` or `T` leave it, and so does opening another photo.

<a id="contrast-as-a-paper-grade"></a>
**Contrast as a paper grade.** Contrast is shown the way darkroom printers set it, as a paper grade from 0 (softest) to 5 (hardest), with 2 as normal. Each whole grade is the contrast of a real paper through one of its filters: the ISO range *R* of Ilford Multigrade RC Deluxe (current), from Ilford's MULTIGRADE RC papers technical information (ISO 6846, filters 0 to 5). A paper with a longer range of exposure is softer, so a grade's multiplier is 90 ÷ *R*, which makes grade 2 exactly ×1.00:

| Grade | ISO range *R* | Multiplier |
|---|---|---|
| 0 | 130 | ×0.69 |
| 1 | 110 | ×0.82 |
| 2 | 90 | ×1.00 |
| 3 | 70 | ×1.29 |
| 4 | 60 | ×1.50 |
| 5 | 50 | ×1.80 |

Between two whole grades the multiplier moves by the same ratio at each step, so grade 3½ is ×1.39, the geometric mean of ×1.29 and ×1.50. The slider snaps to half grades. The row reads "Grade 3 · ×1.29", "Grade 3½ · ×1.39", or with a value between half grades, such as one from a recipe, one decimal: "Grade 2.7 · ×1.19". Past the ends it reads "Softer than grade 0 · ×0.60" or "Harder than grade 5 · ×2.00" and says **(beyond the slider)**. A value that is not on a half grade stays as it is until you move the slider.

Only the display is new. What Prism stores, and what the engine uses, is still the multiplier, the slope at middle grey: a [recipe](#recipes) holds it as `[contrast]` `amount`, and `--contrast` on the command line takes it. A recipe written with `amount = 1.29` shows as grade 3 in the window.

**Typing a value.** Ctrl-click (Cmd-click on a Mac) a slider of Exposure, White balance, Saturation or Contrast to type a value in the unit the row's value line uses (for White balance, the number beside the slider): stops for Exposure, Kelvin or tint for White balance, the factor for Saturation, a paper grade for Contrast. Examples:

| Row | You can type | Meaning |
|---|---|---|
| Exposure | `1.5`, `+1.5` or `1.5 stops` | +1.50 stops |
| Exposure | `-0.5`, `−0.5` (with a true minus sign) or `-0.5 stops` | −0.50 stops |
| Exposure | `1 stop` | +1.00 stops |
| White balance, Kelvin | `5200`, `5200 K`, `5200K` or `5 200` | 5200 K (to the nearest 10 K) |
| White balance, Tint | `10`, `+10`, `-10` or `−10` | tint +10, or −10 |
| Saturation | `1.2`, `1.2×` or `1.2x` | ×1.20 |
| Saturation | `0` | black and white |
| Contrast | `3`, `3.5`, `3,5`, `3½` or `grade 3` | grade 3, 3½ (a number outside 0 to 5 stops at the end) |
| Contrast | `×1.2`, `1.2x` or `1.2×` | the multiplier ×1.20 |

A comma works as the decimal mark (`1,5`). The word is `stops` or `stop`, in any case, and the minus can be a hyphen or a true minus sign (−). Enter confirms, Esc cancels, and a value past the slider's end stops at the end. Text that isn't a number in the row's unit leaves the value as it was. The field opens with the current value, for example `+0.70 stops`, `1.20` or, for Contrast, `3½`. The Tone curve's own slider does not take typed values.

**Resetting.** Right-click a slider for **Reset to** its default (`+0.00 stops`, `As shot`, `×1.00`, `Grade 2 · ×1.00`). Right-click a Tone curve tab for **Reset to None**. The curve's own slider has a **Reset to** menu as well.

**The Tone curve.** The tabs choose the curve; choosing one is one undo step. **Gamma** and **Reinhard** have a slider of their own under the tabs, with its value on the line above: Gamma's exponent (0.5 to 4, default 2.2) and Reinhard's white point (1 to 16, default 4). **None** and **Filmic** have no setting.

**Clip marks.** Under a row's controls, a line says what is out of range at that stage. Each mark is a dot, a share of pixels and a few words, for example "7.0% above white here". The dot's colour says what: orange for **above white**, blue for **below black**, violet for **outside sRGB** (colours too saturated for sRGB). A **hollow** dot means the values are out of range but a later stage may still bring them back. A **filled** dot means this stage cut them off and the detail is gone. Where the row is too narrow for the words, only the shares show. A row with nothing out of range shows nothing. Point at the marks for what each means. At Decode, highlights already at white in the file count as lost. Clip marks are not shown while the [test strip](#the-test-strip) is on.

**Rows after the viewed stage.** A stage after the one you are viewing can't change what you see, so its row is dimmed (it still works), and pointing at it says "Not visible here: you're viewing after Exposure." With a comparison, a row is dimmed only when it is after both pictures.

### Dodge and burn with the Brush

In the darkroom, a dodging card under the enlarger holds light back from one part of the print, so that part comes out lighter; burning in gives a part more time, so it comes out darker. Prism's **Dodge & burn** stage does the same on the photo: you paint with the **Brush**, and each stroke moves the light under it by its strength, measured in stops, right after Exposure. It is local, so it works on the parts you paint and leaves the rest alone. Strokes that overlap add up: two dodges of 0.5 stop make +1.00 stops where they cross.

**Turning the Brush on.** Press `B`, choose **View → Brush**, or press **Brush** on the Dodge & burn row. A ring follows the cursor over the photo. It is not available while you view Decode or Exposure, because the strokes' effect is not in the picture there: view Dodge & burn or any later stage. The button and menu item are greyed out, and a tooltip says why. Turning it on leaves framing on the easel, Sample, Pick neutral and the test strip. If the Dodge & burn switch is off, your first stroke switches it on, in the same undo step, so what you paint shows.

**Painting.** Drag to paint a stroke; a click is a dot. A bar at the top of the photo shows two tones: **Dodge (lighter)** and **Burn (darker)**. Click one, or press `X`, to swap them. Hold `Option` on macOS, `Alt` on Windows and Linux, as you press to paint that one stroke in the other tone; the next stroke goes back to the base tone. The bar's second line says what the next stroke will do, for example "Dodge 0.5 stop — lighter, like holding back light" or "Burn 1.5 stops — darker, like more time under the enlarger".

**Strength** is the slider on the same row, from 0.1 to 2 stops in steps of 0.1, starting at 0.5 stop. A stroke of 1 stop moves the light as much as doubling or halving the time. Right-click it for **Reset to 0.5 stop**. It belongs to the Brush, not to the photo: undo does not change it.

**Size and edge.** `]` makes the Brush bigger and `[` smaller, each press by a factor of 1.2, from 0.5% to 25% of the photo's longer side (it starts at 5%). `Shift+[` makes the edge softer and `Shift+]` harder, by a tenth, from 0 (hard) to 1 (the whole radius fades); it starts at 0.5. The outer ring is the Brush's size. The inner ring is the part at full strength, and the light falls off between them. With a hard edge, or a very small brush, there is only one ring.

**Seeing your strokes.** While the Brush is on, hold `M` and the photo is tinted where you have painted: warm yellow where you dodged, violet where you burned, stronger as the stroke moves the light more, at full colour from 2 stops. It shows whether the Dodge & burn switch is on or off, and goes when you let go of the key or start typing in a field. With **Compare** on, the tint is over the right half only, the viewed stage.

**Moving around.** Hold `Space` and drag to move the picture. Pinch or the wheel zoom as usual. A plain drag paints even when you are zoomed in, and double-clicking does not zoom, since each click is a dot.

**Undo.** One stroke is one undo step (`Cmd+Z` on macOS, `Ctrl+Z` on Windows and Linux; see [Undo and redo](#undo-and-redo)). `Esc` while you hold the button cancels that stroke and leaves no step. `Esc` otherwise leaves the Brush. So do `C`, `I` and `T`; `W`, where Pick neutral can start (White balance or a later stage in view, so not while you view Dodge & burn, where `W` does nothing and the Brush stays on); viewing Decode or Exposure; and opening another photo. The Dodge & burn switch turns all the strokes off and on without losing them.

**What it doesn't do.** **Clear** on the row removes every stroke, as one undo step. Strokes are part of the photo's edit, and an export includes them, at any size, as long as the switch is on. They are not saved in a [recipe](#recipes): a recipe holds Exposure, Colour matrix, Saturation, Contrast and the Tone curve, and White balance if you chose one, and the Save Recipe sheet says "Dodging and burning isn't included." (or "Framing and dodging and burning aren't included." when the photo is also framed). Loading a recipe or choosing a look leaves your strokes as they are. They are lost when you open another photo or quit, and Prism asks first (see [Unexported edits](#unexported-edits)).

### Frame and straighten on the easel

In the darkroom, the easel holds the printing paper under the enlarger, and its four metal blades set the edges of the print. Prism's **Easel** does the same: it frames and straightens the photo, and every stage after it sees only the framed print.

**Opening the easel.** Press `C`, choose **View → Frame**, or click the **Easel** line (or its **Frame** button) at the top of the Light path, above Decode. It works at any viewed stage, Decode included. Without a frame the Easel line reads **Whole photo**; with one it shows the shape and the pixel size when upright, or the shape and the turn when straightened, for example `Free · 5120 × 3413` or `4:5 · 1.2° clockwise` (for a RAW file whose exact sensor size is not yet known, the size starts with `about `). While framing is on, the canvas shows the whole photo again, with a 60% canvas-grey veil over what is cut away outside the blades, and a bar at the top of the canvas holds the framing controls. A live line on the bar gives the frame's full-resolution size, its share of the photo's area and the turn, for example `Frame 5120 × 3413 of 6000 × 4000 (73% of the photo), turned 1.2° clockwise.`

**Blades, handles and moving.** The frame has eight handles: four at the corners and four in the middle of the edges.

- **Corner handle:** drags that corner while the opposite corner stays put. When an aspect ratio is locked, the frame keeps that shape.
- **Edge handle:** moves that one blade on its own. Moving an edge handle drops the aspect lock to **Free**, because moving one edge changes the shape.
- **Inside the frame:** drag anywhere inside the blades to slide the whole frame over the photo without changing its size or shape.

A press without moving changes nothing and keeps the aspect lock. Neither blade can make the frame smaller than 1% of the photo's shorter side (and at least 1 pixel).

**Aspect lock and turning (`X`).** The **Aspect** menu on the framing bar chooses the shape of the blades:

- **Original — the photo's own shape** (the default)
- **Free — any shape**
- **1:1 — a square**
- **4:5 — an 8×10 inch sheet**
- **5:7 — a 5×7 inch sheet**
- **3:2 — a 35 mm negative**
- **16:9 — a widescreen picture**

Each shape follows the photo's orientation: `4:5` is wider than tall (5:4) on a landscape photo, and taller than wide (4:5) on a portrait photo. Press `X`, or click the **⇄ X** button beside the menu, to turn the shape between landscape and portrait around the frame's centre. Moving an edge handle drops the menu to **Free**; dragging a corner or inside the frame keeps the lock. If you choose a lock on the uncropped photo, Prism remembers it while that photo stays open.

**Straightening.** The **Straighten** slider on the framing bar turns the photo from `−45°` to `+45°` in steps of `0.1°`. The blades stay square on the screen while the picture turns under them, about the frame's centre. Positive angles turn the photo clockwise on screen, and the readout beside the slider always names the direction in words: `1.2° clockwise`, `0.5° anticlockwise`, `under 0.1° clockwise` (for a smaller non-zero angle), or `upright` at `0°`.

To straighten along something in the picture that should be level or upright (a horizon, the edge of a building, a door frame), hold `Option` on macOS, `Alt` on Windows and Linux, and drag a line along it on the photo. Letting go turns the photo so that a line within 45° of horizontal becomes level, and a steeper line becomes upright (up to ±45°).

**Typing an angle.** Ctrl-click (Cmd-click on a Mac) the **Straighten** slider to type an angle. You can type a signed number of degrees (`1.25`, `+1.25`, `-0.5`, `−0.5` or `1.25°`, with a dot or a comma for the decimal mark), or an unsigned number followed by `clockwise` or `anticlockwise` (`1.25° clockwise`, `0.5 anticlockwise`, in any case). A sign together with a direction word is refused, and a number past ±45° stops at 45°. `Enter` confirms and `Esc` cancels.

**Why the corners never show.** Whenever you turn the photo, move a blade or change the aspect ratio, Prism closes the blades in just enough (keeping their shape) that no corner of the frame runs past the edge of the turned photo. When that makes the frame smaller or moves it from what you asked for, a note at the foot of the canvas says: `The blades closed in so no corner runs past the photo's edge.` During a single drag of the Straighten slider, every step starts from the frame as it was when you pressed, so turning to 3° and back to 1° in one drag gives back the 1° frame. At `0°` the framed pixels are copied directly with no resampling; when the photo is turned, Prism resamples in linear light (Catmull-Rom over the 4×4 neighbours, clamped to the 2×2 nearest pixels so turning never overshoots or clips).

**Guides.** Guides appear inside the frame only while you are dragging, and disappear as soon as you let go:

- **Rule of thirds** (two vertical and two horizontal lines) while you drag a corner, an edge or inside the frame.
- **8×8 grid** (seven vertical and seven horizontal lines) while you drag the Straighten slider or `Option`-drag (`Alt`-drag) a horizon line, so you can line edges up across the picture.

**Reset, keeping the frame, and undo.** **Reset** on the framing bar sets the angle back to `0°` (`upright`) and opens the blades to the largest centred frame of the current aspect lock (the whole photo for **Original** and **Free**).

Every way out of framing keeps the frame: pressing `Return`, clicking **Done (Return)** on the framing bar (shortened to **Done** on a narrow canvas), pressing `C` again, pressing `Esc` when you aren't dragging, or switching to another tool (`I` or **Compare**; `T`, `B` or `W` where that tool can start — Exposure or a later stage in view for `T`, Dodge & burn or later for `B`, White balance or later for `W`; at an earlier viewed stage the key does nothing and framing stays on). Viewing another stage in the Light path, or using Undo or Redo, leaves framing on.

Each handle drag, move inside the frame, horizon line, Straighten slider drag, typed angle, aspect choice, `X` turn and **Reset** is one undo step (if it changed the frame). Pressing `Esc` in the middle of a drag (a handle, an inside move, a horizon line or the Straighten slider) cancels only that drag, puts the frame back as it was before you pressed, leaves no undo step, and keeps framing on.

**What sees the frame.** Once you leave the easel, every stage from Decode to Output encode, the scopes and histogram, the clipping numbers and `J` marks, Sample colour, Pick neutral, the test strip and Compare all work on the framed photo alone, so a bright sky you cropped out no longer pulls the histogram or the clipping counts. While you are inside framing mode, the canvas shows the whole photo, so the scopes measure the whole photo too (`Scopes (whole photo, while framing)`). Because the preview is at most 2560 pixels on its longer side before framing, a tight crop has fewer preview pixels; pointing at the zoom chip or reading a pixel in Sample mode reports the frame's size instead of the whole file's (for example, `Preview 1800 px, frame 5120 px on the long side: each preview pixel stands for about 2.8 frame pixels across, so a tight frame looks softer here than it exports`). Zoomed in, the canvas shows the frame's own pixels instead (see [Zoom and pan](#zoom-and-pan)). Export always frames the full-resolution decode first.

**Strokes stay on the subject.** Brush strokes from [Dodge & burn](#dodge-and-burn-with-the-brush) are kept in the whole photo's coordinates, not the frame's. If you paint on a face and later crop or straighten the photo, the strokes stay on the face; strokes that end up outside the blades are kept and come back if you open the frame out again.

**The easel is not a stage, and recipes don't hold a frame.** The Easel sits above the stage list in the Light path because framing chooses which part of the negative reaches the paper — it does not change the light of any pixel. Framing a photo is a per-photo composition choice, like brush strokes:

- It is not saved in a [recipe](#recipes). Framing after you load a recipe does not mark the recipe **(modified)**, and loading a recipe or choosing a look leaves your frame as it is.
- When the photo has a frame, the Save Recipe sheet says "Framing isn't included." (or "Framing and dodging and burning aren't included." if the photo also has brush strokes).
- When you close the photo or open another, Prism asks before losing the frame, even straight after an export (see [Unexported edits](#unexported-edits)).
- On the command line, `--rotate`, `--crop` and `--aspect` frame an export and combine with `--recipe` (see [CLI.md](CLI.md#--rotate-degrees)).

### Compare two stages

At the right of the Light path's header, **Compare: Off** shows the comparison and opens a menu: **Off**, **Original** (the file as decoded, before any edit), each stage ("after Exposure" and so on), then **Swap sides**. Once chosen, the header reads **Compare:** and the choice, for example "Compare: Original". The photo splits in two at the same size and position, so the halves line up:

- the compared stage is on the **left**, the viewed stage on the **right**, each with a label in its corner;
- **drag the divider** to move it (it stays between 5% and 95% of the width of the picture on the canvas, so zoomed in it stays in reach and the picture moves under it);
- **Swap sides** exchanges the halves (greyed out while the comparison is off);
- **Off** (or choosing the viewed stage again) goes back to one picture;
- `Esc` clears the comparison first, then the viewed stage.

The scopes, clipping numbers and the `J` marks describe the viewed stage only, which is the right half (the marks are drawn over the right half). A comparison is only a view: it is not saved, it can't be undone, it goes back to Off when you open another photo, and it does not affect export.

### Zoom and pan

The photo opens fitted to the window. To look closer:

| To | On a trackpad | With a mouse | Keys |
|---|---|---|---|
| Zoom in or out, about the cursor | pinch (or `Cmd` and scroll) | the wheel | |
| Zoom in or out, about the middle | | | `=` or `+`, and `-` |
| Move the picture | scroll with two fingers | drag it, or hold `Space` and drag | |
| Fit the whole photo | | double-click at 100% | `F` |
| Go to 100% (real pixels) | | double-click | `1` |

On Windows and Linux the zoom key is `Ctrl`. Windows has no pinch of its own here: use `Ctrl` and the wheel.

A chip in the corner of the canvas shows the zoom, with **Fit** and **100%** buttons, and **View → 100% (real pixels)** does the same as the button. The number is a percentage of the photo's own pixels, so Fit on a large photo reads, for example, 25%. You can go from fit up to 800%. Past 200% the picture stops smoothing, so you see its pixels as squares. Zooming and moving never re-render the preview, so they stay smooth however heavy your edits are.

**100% is the photo at its full size: one pixel of the photo on each pixel of your screen.** On a Retina screen a photo pixel is half a point across. Fit looks just as it always has: the whole photo, drawn from the preview, which is at most 2560 pixels on its longer side.

**Real pixels.** Zoom in past the preview's own size and Prism draws the photo's own pixels, rendered from the full-size photo through the frame and every stage. At 100% and above each one is exactly the pixel an export at the photo's own size has there. Right after you open a photo, Prism reads it again at full size in the background: about a second, and about 0.7 GB of memory, for a 60 megapixel RAW file. While real pixels are on their way you see the preview enlarged, never a blank. Move the picture and the new edge shows the preview for a moment; drag a slider or paint a stroke and real pixels follow a moment behind. An export started while the photo is being read still works: the read pauses for it and starts again after.

**"· preview" on the chip**, for example "400% · preview", means the canvas shows the preview enlarged for a reason that lasts, and pointing at the chip says which:

- the full-size photo is still being read;
- it couldn't be read or didn't fit in memory, or it would take more than a quarter of this computer's memory;
- the part on screen is over 30 megapixels: zoom in further;
- the part on screen is over 8 megapixels while you drag, scroll or paint: real pixels come when you let go.

**What stays on the preview.** The scopes, the histogram and the clipping numbers always describe the whole picture, from the preview. The easel, the test strip, Compare, an earlier stage in the Light path, the clipping marks (`J`) and the stroke mask (`M`) show the preview, enlarged when you zoom in, with no "· preview" on the chip. A photo no bigger than the preview has nothing more to show: its preview is the photo itself.

Point at the chip and the Explain area also says how the preview compares and what the canvas shows, for example "Preview 2560 px, file 9564 px on the long side: each preview pixel stands for about 3.7 file pixels across. Showing the photo's own pixels."

Dragging follows a few rules:

- A plain drag moves the picture only when you are zoomed in. It starts after a small movement, so a click stays a click.
- The comparison's divider wins over a plain drag: press on it and you move the divider. Hold `Space` and drag to move the picture from anywhere.
- `Space` belongs to the picture only while the cursor is over it and you aren't typing or using a control. Elsewhere it still presses the focused button.
- Double-clicking does nothing on the test strip or the divider.

Zoom is only a view: it is not saved, it can't be undone, it goes back to fit when you open another photo, `Esc` leaves it alone, and it does not affect export.

### Read a pixel

Press `I`, or choose **View → Sample Colour**, and point at the photo. A **Sample** block appears under the Light path (it is there only while Sample mode is on) and reads the pixel under the cursor at the stage you are viewing; until you point at the photo it says "Point at the photo to read a pixel; click to pin it." Click to pin that pixel: the block then stays on it while you change the photo, so you can watch one spot of sky as you move Exposure. With a pin set, while the cursor is over the picture, one more line says what is under it. There is one pin; another click moves it. `Esc` or `I` again leaves Sample mode and clears the pin.

The numbers come from the same linear values the pipeline works on, not from the 8-bit picture, and they match the render exactly. For example:

> R 1.210 G 1.050 B 0.860 (linear, 1.00 = white)
> 107% of white's light, 2.6 stops above middle grey
> Red and green are over white. Not lost yet: the Tone curve (filmic) brings them back under 1.00.
> One preview pixel averages about 3.7×3.7 file pixels, so a small clipped highlight can hide in it.

- **The values** are [linear light](#linear-light): 1.00 is white and 0 is black, and a value can go past either end until a stage cuts it off. At Output encode you also get the 8-bit sRGB codes the file will hold.
- **"Of white's light"** is the pixel's luminance (0.2126 R + 0.7152 G + 0.0722 B) as a share of white. It is a measure of light, not of how light the tone looks: middle grey is 18% of white's light, though it looks about halfway.
- **Stops from middle grey** say where the pixel sits against an 18% grey card, in [stops](#stops). 0.36 is 1.0 stops above it and 0.09 is 1.0 stops below.
- **From the Tone curve on**, 18% is no longer a grey card, because the curve has moved the values. So the block says the two things apart: "On screen 30% of white's light", which can't go above 100%, and "In the scene 0.7 stops above middle grey", taken from the Contrast stage.
- **The last sentence** says if the pixel is over white, has no light, or is too saturated for sRGB, and which later stage brings it back or clips it. It uses the same test as the clipping marks (`J`) and the clipping note, so they never disagree.
- **A preview pixel is an average.** The preview is smaller than a large file, so one preview pixel stands for several file pixels, and the block says how many. A pixel reading under white can still hide a small clipped highlight. Export uses every pixel.
- **On real pixels** (zoomed in to 100% or more, where the canvas shows the photo's own pixels), the block reads the photo's own pixel under the cursor instead, and its last line says "A real pixel: one pixel of the photo at its full size." Its values and sRGB codes are those of the same pixel in an export at the photo's own size. Below 100%, and wherever the canvas shows the preview, it reads the preview pixel as before.

Point at a row and the Explain area says what it means.

- **In a comparison**, the block shows both stages, the one you are viewing first.
- **On the test strip**, it reads the band under the cursor with that band's exposure. You can't place a pin there, since a click on a band sets the exposure. A pin you already have stays.
- **On the picture**, the pin is a small crosshair, or an outline of the pixel's square above 200%, and the pixel under the cursor has a fainter outline; on real pixels the square is the photo pixel's. If you zoom or move the pin off the canvas, its values say "(off screen)". A pin placed at Fit reads, once you zoom in to real pixels, the photo pixel in the middle of the preview pixel you clicked.
- **Dragging and double-clicking** still move and zoom the picture. A drag doesn't pin, but a double-click's first click does move the pin.

The pin is in the photo's pixels, so it stays put through zooming, moving, changing stage and re-decoding with a new camera matrix. Changing the frame on the easel or opening another photo clears it. Sample mode is only a view: it is not saved, it can't be undone, and it does not affect export.

### The test strip

A darkroom test strip prints one frame in bands at different exposures, so you can pick by eye. Prism does the same on the screen: press `T`, click the **Test strip** button in the Exposure row of the Light path (it looks pressed while the strip is on), or use **View → Test Strip**.

- The photo is cut into five bands along its longer side (side by side for a landscape photo, stacked for a portrait), darkest first.
- A bar above the photo picks the step between bands: **⅓**, **½** (the default) or **1 stop**. It also says what that step means, for example "A full stop doubles the light; each half stop is about 1.4×." Its **×** button closes the strip.
- Each band has a label with its offset from your current exposure and the change in light: **−1 stop · ½× light**, **−½ · 0.71×**, **0 · now**, **+½ · 1.41×**, **+1 · 2×**. The middle band is your photo as it is now.
- The band under the cursor is outlined, and the scopes show that band (the middle one when the cursor is elsewhere). The Scopes title says which, for example *Scopes (−½ stop band)*.
- While the strip is on, the Light path shows no clip marks, since each band has its own light.
- Press `J` to mark clipping in each band, to see which exposure starts to blow the highlights.
- **Click a band** to use its exposure. This is one undo step; the strip stays on, centred on the new exposure. Clicking the middle band does nothing.

The strip works zoomed in too: each band on the canvas keeps its label. A plain drag doesn't move the picture while the strip is on, because a click there sets the exposure; hold `Space` and drag instead.

The strip needs the Exposure stage or a later one in view: while you are viewing Decode no edit shows, so the strip is greyed out, and a tooltip says why. Turning the strip on clears a comparison. `Esc`, the **×** button or `T` again turn it off. The strip is only a view: it is not saved, it can't be undone, it goes away when you open another photo, and **export is never affected**.

### The status bar

The bar at the foot of the window has three parts. Before a photo is open it says "No photo open".

- **Left:** the file's name, the size of the decoded image and its kind, for example `IMG_0001.jpg · 6000 × 4000 · JPEG` (the kind is RAW, JPEG, PNG or HEIC). A long name is shortened in the middle and keeps its extension. Point at it for the file's full path, the size of the preview and how long the last preview took, and, for a RAW file, "RAW, half-size preview".
- **Centre:** the camera line, from the file's own metadata: the camera model, focal length, aperture, shutter time and ISO, for example `ILCE-7RM5 · 70 mm · f/10 · 1/250 s · ISO 100`. A field the file doesn't have is left out, and the whole line is hidden if the file has none or if the window is too narrow to show it without touching the other parts. The focal length adds the 35 mm equivalent when it differs from it by more than 10%: `6.8 mm (24 mm equiv.)`. Point at it for the make, model, lens and the date the photo was taken.
- **Right:** the clip note, which says in a few words what clipped, with a dot in the colour of the kind: orange for highlights, blue for shadows, violet for colours too saturated for sRGB. It reads, for example, "2.7% clipped in the file" (highlights already at white in the file), "3% highlights clipped at Output encode", "0.4% shadows clipped at Output encode" or "No clipping". With more than one kind it names the first and adds "+1 more". Point at it for the full sentence. It follows your edits, and is muted for "No clipping" and while the picture is being rendered again.

### The Explain area

At the foot of the panel, a box says in plain words what the control under the mouse does to the light. It has a one-line summary, a short detail, and a live line worked out for the current value. For example, pointing at Exposure at +0.70 stops reads "+0.70 stops, 1.62× the light". It follows the control you hover over, else the focused one, else the last one it showed, so moving across the gap between two controls keeps the text you were reading. It is always about five lines tall, so the panel above it never changes size, and it scrolls when a text is longer. Tooltips show just the summary, after the same short delay everywhere. You can switch the Explain area off in [Preferences](#preferences); tooltips stay.

### Undo and redo

The **Undo** and **Redo** buttons in the panel's header, and **Edit → Undo** and **Redo**, do the same. Every change to a slider, a stage switch, a look or the tone curve is a step, and so is loading a recipe, painting a stroke, clearing the strokes, and each framing change on the easel; Prism keeps up to 100. A whole slider drag or blade drag is one step. Test strip, comparison and the stage you are viewing are views and are not part of undo.

### Export

**Export…** in the panel's header, or **File → Export…** (`Cmd+E` or `Ctrl+E`), saves the photo with your edits as a PNG or JPEG. It first opens the **Export sheet**, where you choose the size, and **Save As…** then opens the system's save dialog.

**The Export sheet** has a **Size** control with three choices:

- **Original** (the default) saves the photo at its own size (or the frame's size, when the photo is framed). The sheet says what that is: "Full size, 6000 × 4000", or with a frame, "Full size of the frame, 5120 × 3413".
- **Long side** saves it with its longer side that many pixels (1 or more), keeping the shape: 2048 gives 2048 × 1365 for a 6000 × 4000 photo, and 1365 × 2048 for the same photo held upright.
- **Percent** saves it at that share of the photo's own size (or of the frame's size, when framed), from 1 to 99. 50% of a 6000 × 4000 photo is 3000 × 2000. The percentage is of the full size, not of the preview, and for a RAW file it is of the camera's full sensor size, even when half-size decoding is on.

A line under the number says what will be written, for example "Exports at 2048 × 1365". Prism never enlarges: if the size you ask for is not smaller than the photo (or the frame), the line says "Not smaller than the photo: exports at full size, 6000 × 4000" (or "Not smaller than the frame: exports at full size, 5120 × 3413") and the photo is saved as it is. For a RAW file the sizes shown are those that will be written: the full sensor size, or, if the half-size RAW option is on, the half-size decode, whatever size you ask for. A typed number outside the range is moved to the nearest valid one when you leave the field (0 becomes 1; a percentage of 150 becomes 99). The photo is framed at full resolution first, then reduced by averaging the light in each area, before the edit stages run.

`Enter` is **Save As…** and `Esc` or **Cancel** closes the sheet and changes nothing. Prism remembers your choice, also the next time you start it, so the sheet opens on it. While the sheet is open, the other keys (`C`, `I`, `T`, `W`, `B`, `J`, zoom, undo) do nothing.

- The file name is suggested as the photo's name plus `_prism` next to the original (`IMG_0001.jpg` becomes `IMG_0001_prism.jpg`). An export that is reduced says so with the longer side added: `IMG_0001_prism_2048.jpg`; one that is not reduced has no number. A PNG source suggests PNG; anything else, including RAW, suggests JPEG.
- If you type a name without `.png`, `.jpg` or `.jpeg`, the extension is added.
- Prism won't overwrite the photo you opened: choose another name.
- Export decodes the file again at **full size** (then reduces it, if you chose a smaller size) and runs every stage, so it does not depend on the preview. A **Saving…** window shows while it runs, with a **Cancel** button.
- The result is 8-bit sRGB. JPEGs are saved at quality 95.

### Recipes

A **recipe** is a small text file, ending in `.prism`, that writes down an edit: the exposure, the colour matrix, the saturation, the contrast and the tone curve, each with its on/off switch, and the white balance if you chose one. It holds numbers, not pixels, so you can use it on any photo, read it in a text editor and send it to someone. [RECIPES.md](RECIPES.md) describes the format line by line.

**Saving.** **File → Save Recipe…** (`Cmd+Shift+S` or `Ctrl+Shift+S`), or **Save recipe…** in the Look menu, first asks for two things:

- **Name**, filled in for you: the recipe you loaded (with "(modified)" once you have changed something), or the look you picked, or empty with the hint "Name this look". The suggested file name is made from it.
- **Description** (optional): one line on what the recipe is for, filled in from the recipe you loaded or the look you picked. If you keep both for a built-in look, the file you save is that look's file plus a `[matrix]` section.

The sheet says what is saved: all five stages, and the white balance you chose if you chose one. A photo left **As shot** saves no white balance, because as shot is that photo's own light and not a number to carry to another photo; such a recipe is the same file older versions of Prism wrote, and they can still read it. The photo, the RAW decoding options (half size, camera matrix), the frame on the easel and any brush strokes are not in a recipe; when the photo has a frame or strokes, the sheet adds "Framing isn't included.", "Dodging and burning isn't included." or "Framing and dodging and burning aren't included." `Esc` cancels; while you are typing in a field, the first `Esc` only leaves the field. **Save…** then opens the system's save dialog. It starts in the folder you last saved or loaded a recipe in (Prism remembers it in your [preferences](#preferences)), or next to the photo the first time. If you type a name without `.prism`, it is added. A banner says "Saved recipe *name*.prism." when it is written.

**Loading.** **File → Load Recipe…** (`Cmd+Shift+O` or `Ctrl+Shift+O`), or **Load recipe…** in the Look menu, applies a recipe to the open photo:

- It sets each stage the recipe has, with its switch, and leaves the others as they are. It is one undo step, and the photo is not decoded again.
- The Look menu's button reads **Recipe:** *name*, with a line under it saying what the recipe set (point at the line for the recipe's description): "Sets all five stages." or, for a recipe with only some of them, "Sets only Colour matrix, Saturation and Contrast; the other stages are unchanged." A recipe that switches a stage off says so too.
- A recipe with a white balance sets that light on this photo too ("Sets all five stages and White balance."), whatever light this photo was shot under. A recipe without one leaves the photo's white balance as it is.
- Change anything and the button reads *name* **(modified)**; undo back past the load and it goes away, and Redo brings it back. Opening another photo clears it.
- The Look menu stays honest: it names a built-in look only when the numbers match one the loaded recipe isn't; a look you pick afterwards shows its own name.
- A colour matrix from a recipe shows in the Colour matrix row under the file name the recipe gives, or as "From recipe" when it gives none.

**Dropping.** Drop a `.prism` file on an open photo to apply it, just like Load Recipe…. Prism applies one recipe at a time. A recipe dropped together with a photo isn't applied (the photo opens, and a note asks you to drop the recipe again once it is open), and with no photo open Prism asks you to open one first.

**When a recipe has a mistake**, nothing changes and a **Recipe not loaded** window lists every mistake with its line, for example "line 5: unknown setting 'amout' in [saturation]. Did you mean 'amount'?" (Prism stops at 10). **Copy** puts the list on the clipboard; **Close** or `Esc` closes the window. A missing or unreadable file is reported the same way.

### Unexported edits

If you quit (`Cmd+Q` on macOS, `Ctrl+Q` on Windows and Linux, the close button or **File → Quit**) or open or drop another photo while your settings differ from the last export and from the last recipe you saved (or, if you have saved neither, from when you opened the photo), Prism asks: "You have edits that haven't been exported or saved as a recipe." Under it, a line says what you saved last, for example "Last saved: exported as IMG_0001_prism.jpg" or "Last saved: recipe Warm grade.prism". The buttons:

- **Export…** goes on with the quit or open only after the export succeeds.
- **Save Recipe…** shows the save sheet and then the save dialog; the quit or open goes on once the recipe is written. Cancelling either, or a recipe that can't be written, cancels the quit or open.
- **Quit Without Saving** (or **Open Without Saving**).
- **Cancel** (also `Esc`).

On macOS **Quit Without Saving** stands on its own on the left, and Cancel, Save Recipe… and Export… are on the right; on Windows and Linux the order is Export…, Save Recipe…, Quit Without Saving, Cancel. Export… is the default (`Enter`) on both.

A frame on the [easel](#frame-and-straighten-on-the-easel) and strokes from the [Brush](#dodge-and-burn-with-the-brush) change this. No recipe keeps them and an export does not make them safe to lose, so while the photo has either, Prism asks before it quits or opens another photo, even straight after an export, and adds: "Framing isn't saved in recipes and is lost when the photo closes.", "Dodging and burning isn't saved in recipes and is lost when the photo closes." or "Framing and dodging and burning aren't saved in recipes and are lost when the photo closes." The buttons are then **Export…**, **Quit Without Saving** (or **Open Without Saving**) and **Cancel**, with no **Save Recipe…**.

Undoing back to an exported or saved state counts as having no edits. If an export is still running, Prism waits for it first. A quit while the recipe save sheet is open closes the sheet before asking.

## Keyboard shortcuts

On macOS the main key is `Cmd`; on Windows and Linux it is `Ctrl`. This is every shortcut in the app.

| Action | macOS | Windows / Linux |
|---|---|---|
| Open a photo | `Cmd+O` | `Ctrl+O` |
| Export | `Cmd+E` or `Cmd+S` | `Ctrl+E` or `Ctrl+S` |
| Load a recipe | `Cmd+Shift+O` | `Ctrl+Shift+O` |
| Save a recipe | `Cmd+Shift+S` | `Ctrl+Shift+S` |
| Undo | `Cmd+Z` | `Ctrl+Z` |
| Redo | `Cmd+Shift+Z` or `Cmd+Y` | `Ctrl+Shift+Z` or `Ctrl+Y` |
| Preferences | `Cmd+,` | `Ctrl+,` |
| Bigger interface | `Cmd+=` or `Cmd++` | `Ctrl+=` or `Ctrl++` |
| Smaller interface | `Cmd+-` | `Ctrl+-` |
| Interface size back to Auto | `Cmd+0` | `Ctrl+0` |
| Mark clipping on the photo | `J` | `J` |
| Test strip on or off | `T` | `T` |
| Sample mode on or off (read a pixel) | `I` | `I` |
| Pick mode on or off (click a neutral to set the white balance) | `W` | `W` |
| Zoom in | `=` or `+` | `=` or `+` |
| Zoom out | `-` | `-` |
| Zoom to fit | `F` | `F` |
| Zoom to 100% (real pixels) | `1` | `1` |
| Move the picture while held, with a drag | `Space` | `Space` |
| Frame on the easel on or off | `C` | `C` |
| Framing: finish and keep the frame | `Return` | `Return` |
| Framing: turn the aspect ratio between landscape and portrait | `X` | `X` |
| Framing: drag a horizon line to level or make upright | `Option` and drag | `Alt` and drag |
| Brush on or off (paint dodging and burning) | `B` | `B` |
| Brush: swap Dodge and Burn | `X` | `X` |
| Brush: paint one stroke in the other tone | `Option` held at the press | `Alt` held at the press |
| Brush: bigger or smaller | `]` or `[` | `]` or `[` |
| Brush: harder or softer edge | `Shift+]` or `Shift+[` | `Shift+]` or `Shift+[` |
| Brush: show the strokes while held | `M` | `M` |
| Step back: close the Preferences, About, notices, Recipe not loaded or View recipe window if it has the focus; otherwise framing on the easel (a drag under way is cancelled first; otherwise leaves framing and keeps the frame), then the Brush (a stroke under way is cancelled first), then Pick mode, Sample mode, the test strip, then the comparison, then the viewed stage | `Esc` | `Esc` |
| Quit | `Cmd+Q` | `Ctrl+Q` |

Notes:

- The keypad's `+`, `-` and `0` also work with `Cmd`/`Ctrl` for the interface size, its `+` and `-` alone for the zoom, and its `Enter` for finishing framing.
- Undo, Redo, `C`, `J`, `T`, `I`, `W`, `B` and the zoom keys do nothing while you are dragging a slider, dragging on the easel or painting a stroke.
- `C`, `J`, `T`, `I`, `W`, `B`, `X`, `Return`, the zoom keys and `Space` give way to a text field that has the keyboard, so typing a value never triggers them.
- `Esc` is also Cancel in the "unsaved edits" window, in the recipe save sheet (inside a text field, the first `Esc` only leaves the field), in the Export sheet (also inside its number field), and in the "Saving…" window while a quit or open is waiting for the export.
- In the menus: **File** has Open…, Export…, Load Recipe…, Save Recipe… and Quit (on Windows and Linux the menu shows `Ctrl+Q`; on macOS, Quit is in the **Prism** menu too). **Edit** has Undo, Redo and (on Windows and Linux) Preferences…. **View** has Bigger Interface, Smaller Interface, Interface Size: Auto, Test Strip, Sample Colour, Pick Neutral, Brush, Frame, Zoom In, Zoom Out, Zoom to Fit and 100% (real pixels). **Help → About Prism** is on Windows and Linux; on macOS, About and Preferences are in the **Prism** menu (**Prism → About Prism**, **Prism → Preferences…**).

Mouse and trackpad: click the Easel line to frame and straighten, drag the blades' handles or inside the frame, and `Option`-drag (`Alt`-drag) along a horizon to level it (see [Frame and straighten on the easel](#frame-and-straighten-on-the-easel)); click a stage row to view it; right-click a slider, or a Tone curve tab, to reset it; Ctrl-click (Cmd-click) a slider to type a value; drag the divider in a comparison; click a band in the test strip; drag the panel's left edge to resize the panel; pinch, scroll or use the wheel over the photo to zoom and move it, drag it when zoomed in, and double-click it to go between fit and 100% (see [Zoom and pan](#zoom-and-pan)); in Sample mode, point at the photo to read a pixel and click to pin it (see [Read a pixel](#read-a-pixel)).

## How Prism thinks about light

### Stops

A **stop** is a doubling or halving of the light. One stop more is twice the light, two stops is four times, minus one stop is half. It is the same unit as the aperture ring and the shutter speed dial. In Prism, Exposure is in stops: +1.00 stops doubles every pixel, and each half stop is about 1.4×. So Exposure at +0.70 stops reads "+0.70 stops, 1.62× the light" in the Explain area. Middle grey, the reflectance of a standard grey card, is 18% in linear light. Contrast works in stops around it. To see where any part of your photo sits against it, [read a pixel](#read-a-pixel).

### Linear light

The numbers in a JPEG are not proportional to light. They sit on a curve (sRGB) that gives more of the numbers to the shadows. Prism undoes that curve first, in the **Decode** stage. After that, the numbers are proportional to light: twice the number is twice the light. In this form, +1 stop is exactly a multiplication by 2. A RAW file is already linear, which is why it looks dark and flat before a tone curve.

Prism works in linear light, with floating-point numbers, through every stage. Only the last stage, **Output encode**, turns the light back into the curved 8-bit sRGB numbers of the file and the screen.

### The order of the stages

The stages always run in this order, whatever order you change them in:

**Decode → Exposure → Dodge & burn → White balance → Colour matrix → Saturation → Contrast → Tone curve → Output encode**

**The easel is not a stage.** The [Easel](#frame-and-straighten-on-the-easel) sits above Decode, outside the stage list, with no dot on the rail, no on/off switch and no clip marks. In the darkroom, the easel's four blades frame the sheet of paper under the enlarger: they decide which part of the negative reaches the print, and move no light of their own. So Prism frames and straightens the decoded photo first, and every stage from Decode to Output encode (along with the scopes, clipping counts, Sample, Pick neutral, the test strip and Compare) sees only the framed print. Because framing is a composition for this one photo rather than a way of handling light, it is not in a [recipe](#recipes) and does not mark a loaded recipe **(modified)**.

The order of the stages matters because they are not interchangeable:

- **Exposure comes first** after Decode, while the numbers are still light. A stop of exposure then means exactly twice the light, and the stages after it can shape what that extra light looks like.
- **Dodge & burn** comes right after Exposure, also in linear light, so a stroke of 1 stop is exactly twice the light, and the stages after it, which shape colour and tone, work on light you have already shaped.
- **White balance** comes next, also in linear light, where a change of light is a simple mix of red, green and blue. It comes before the Colour matrix, so a grade is applied to a photo whose whites are already white.
- **The Colour matrix** mixes red, green and blue in linear light, before saturation and tone. It is for calibrating camera colour or applying a grade.
- **Saturation** moves each colour away from, or towards, the grey of the same luminance. A colour that would go below zero stops at the most saturated version sRGB can show, so hues do not shift.
- **Contrast** is an S-curve around middle grey, in stops. It is strongest within about 2 stops of grey, so the extremes are not crushed.
- **The Tone curve** comes last, because it squeezes scene light to fit the screen. Linear light has more range than a screen shows. The curve compresses it: bright values are pulled in, so highlights have room. If you brightened after the curve instead of before it, you would only be making the compressed picture brighter and pushing highlights past white.
- **Output encode** is last. Anything still above white or below black is cut off here, and cannot come back.

Switching a stage off in the Light path shows you what it contributed.

### What clipping means

A file can only hold values from black to white. In linear light, black is 0 and white is 1. Clipping is when a value falls outside that and is cut off at 0 or 1: **highlights** that were brighter than white, **shadows** that have no light left, and colours **too saturated for sRGB**, where a channel would need negative light (a strongly saturated yellow or orange, pushed further by a colour matrix, can end up with a negative blue; the encode cuts it to 0 and the colour's hue shifts).

A value outside the range is not lost by itself. Prism keeps it as a number, so a later stage may bring it back: darkening with a tone curve can pull a highlight back under white. Detail is lost only where a stage cuts values off at white or black. Until then the values are kept. In the Light path that is the difference between a hollow dot (recoverable) and a filled dot (lost). Saturation and Tone curve cut off values below 0, and Output encode cuts off both ends; Exposure, White balance, Colour matrix and Contrast never do. The Decode stage counts pixels that were already at white in the file (a RAW file's saturated sensor values, or 255 in an 8-bit file) as lost before any edit.

## Preferences

Open **Preferences** from the Edit menu (Windows and Linux) or the Prism menu (macOS), or press `Cmd+,` / `Ctrl+,`. It is an ordinary window; changes apply at once and you can keep editing.

- **Appearance:** System (the default: follows the operating system's light or dark mode, also when it changes while Prism is open), Light or Dark. In every theme the canvas behind the photo stays a neutral grey (slightly lighter in Light), never white, black or tinted, so it does not change how light or colourful the photo looks.
- **Interface size:** Auto (the display's scale, for example 150% on a Windows laptop set to 150%) or 75% to 200% in steps of 25. `Cmd`/`Ctrl` with `+`, `-` and `0` change it too, also from the View menu.
- **Panel width:** the width of the control panel. You can also drag the panel's left edge. The panel never takes more than 40% of the window, and never gets narrower than its texts need: the narrowest width at which every text of the Light path is drawn in full, with the longest values, the clip notes and the Tone curve's tabs (about 430 points, at any interface size). A width saved by an older version, or from a smaller interface size, is widened to it, and the window cannot be made narrower than room for that panel and some photo. **Reset layout** goes back to 440 points.
- **Show explanations** (on by default): the Explain area. Tooltips keep their one-line summary either way.
- **Restore defaults** resets all of the above, and also the scope you chose and the clipping marks.

**Help → About Prism** (on macOS, **Prism → About Prism**) shows the version, Prism's licence and the third-party components with their licences; **Third-party notices…** opens their full notices. `Esc` closes it.

Prism keeps the preferences in a file, `preferences.txt`, in the folder the system gives the app for its data: `~/Library/Application Support/Prism/Prism/` on macOS, `%APPDATA%\Prism\Prism\` on Windows, `~/.local/share/Prism/Prism/` on Linux. It holds `key=value` lines and is safe to edit or delete: a missing or damaged file, or a bad value, falls back to the default for that setting alone, and settings this version doesn't know (from a newer one) are kept. The file is written atomically (a temporary file renamed over it), so a crash doesn't leave half a file. The window layout is kept next to it in `imgui.ini`.

## Reference details

The exact numbers behind what the app shows.

- **Histogram.** Rec. 709 luminance as a filled area, with red, green and blue lines on top, 256 bins each, on the display scale: a value falls in the bin of its sRGB code, so linear middle grey 0.18 sits near bin 118 and the shadows are not squashed. It is measured on the stage's values *before* the 8-bit encode, so it can count what clips separately from the end bins.
- **Scopes.** A scope measures the stage you are viewing, on the display scale (the 8-bit sRGB values on screen). The Parade and Waveform have guide lines at white, 50%, 18%, 5% and black, each at the sRGB code of that share of white's light. The vectorscope has a round graticule, scaled so that the farthest any colour can land is its outer ring, with targets for R, Mg, B, Cy, G and Yl at 75% saturation, and the skin-tone line at about 123°. The readout under the plot counts pixels with no light left (below black) and pixels with a channel above 1 (above white), the same counts as the histogram's corner triangles and the Light path's marks. The counts are drawn log-scaled, so faint detail stays visible.
- **Clipping counts.** They come from a sample of at most 250,000 pixels of the preview. Decode counts any channel within 0.000001 of 1.0, so a value exactly at white in the file counts too. A negative channel on a pixel that still has light is not a shadow: it is a colour outside sRGB.
- **Outside the sRGB gamut.** A pixel counts as too saturated for sRGB when it still has light (luminance above 0) but a channel is below −0.00015 in linear sRGB (half an 8-bit step at the dark end of the sRGB curve; smaller negatives round to 0 anyway). A pixel with no light is a shadow, not a gamut problem. "Pushed out" names the first stage with such colours (usually Colour matrix); "clips" names the first later stage that cuts negatives: Saturation if it runs, otherwise Tone curve, otherwise Output encode. Saturation limits its own result to the edge of the gamut rather than cutting it off, so that is not counted as a loss; only colours that arrive at Saturation already negative are.
- **Marks on the photo.** Where a pixel is in several categories, highlights win over gamut, and gamut over shadows.
- **Test strip labels.** The offsets are exact (a third of a stop is 1/3, not 0.33). A third of a stop is about 1.3× the light, half a stop about 1.4×, a whole stop 2×.
- **Looks.** Each look's numbers are in its recipe file. RAW: [Natural](../recipes/raw/natural.prism), [Original](../recipes/raw/original.prism), [Vivid](../recipes/raw/vivid.prism), [Soft](../recipes/raw/soft.prism), [Black & white](../recipes/raw/bw.prism). PNG and JPEG: [Original](../recipes/image/original.prism), [Vivid](../recipes/image/vivid.prism), [Soft](../recipes/image/soft.prism), [Black & white](../recipes/image/bw.prism). [Why each look is what it is](../recipes/README.md).
