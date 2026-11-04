# The built-in looks

These are the looks in Prism's Look menu, as plain recipes: `raw/` for camera RAW files and `image/` for PNG and JPEG. The build embeds them in the app (`gui_core/CMakeLists.txt` lists them in menu order; the first is what a newly opened file starts with). The format is described in [docs/RECIPES.md](../docs/RECIPES.md).

Each file has exactly four stages: exposure, saturation, contrast and the tone curve. None has a `[matrix]`, because a look leaves the colour matrix and the RAW options alone: those describe the camera, or a grade you loaded on purpose. Each file is written exactly as Prism writes a recipe, so comments in the files can't say why a look is what it is. That is here instead.

The values still need checking on real photos (an a7R V RAW, a few phone JPEGs) and may be tuned later. Every value is one the command line accepts, so each look is also a `prism` command.

## RAW

RAW files are linear: without a curve they look dark and flat. So every RAW look except Original brightens by a stop and adds a curve.

- **Natural**: +1 stop with filmic is the README's starting point for an a7R V file.
- **Original**: no adjustments, so you can see what the sensor recorded.
- **Vivid**: the README's "closer to the camera's JPEG" example.
- **Soft**: Reinhard rolls the highlights off more gently than filmic; with less contrast and colour that gives a low-key, soft look.
- **Black & white**: a little extra contrast, because a greyscale image without colour contrast looks flat.

## PNG and JPEG

A PNG or JPEG already has a tone curve and the camera's colour. So no look adds a curve (that would apply one twice and dull the whites), and the changes are milder than for RAW.
