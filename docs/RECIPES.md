# Prism recipes

A recipe is a small text file, ending in `.prism`, that writes down an edit: how much exposure, which colour matrix, how much saturation and contrast, and which tone curve. It holds numbers only, not pixels, so the same recipe can be used on any photo. You can open it in any text editor, read exactly what it does, change a number, and send it to someone else. Nothing in it is hidden: a look is just its recipe.

Contents: [A recipe by hand](#a-recipe-by-hand) · [As Prism saves it](#as-prism-saves-it) · [Every setting](#every-setting) · [Leaving stages out](#leaving-stages-out) · [The rules of the format](#the-rules-of-the-format) · [Versions](#versions) · [When a recipe has a mistake](#when-a-recipe-has-a-mistake)

## A recipe by hand

This recipe gives a photo a warm grade and softer contrast. The matrix is the one in [`samples/grade.txt`](../samples/grade.txt), which lifts red a little and pulls blue down, so whites and greys lean slightly warm.

<!-- recipe: valid -->
```toml
prism_recipe = 1
name = "Warm low contrast"
description = "Softer contrast and a warm grade, for portraits in open shade"

# The warm grade from samples/grade.txt: each row is one output channel,
# made from the input red, green and blue.
[matrix]
red   = [1.05, 0.03, -0.01]
green = [0.01, 1.00,  0.00]
blue  = [-0.02, -0.02, 0.94]
from  = "grade.txt"

[saturation]
amount = 0.9    # a little less colour

[contrast]
amount = 0.85   # flatter: less difference between light and dark
```

Every recipe starts with `prism_recipe = 1`, the version of the format it is written in. Then come a name and a description, both optional, and one section in square brackets for each stage of the edit. Anything after `#` is a comment, for people only.

This one has no `[exposure]` and no `[curve]`, so it leaves those alone: the photo keeps the exposure and tone curve it already has. See [Leaving stages out](#leaving-stages-out).

## As Prism saves it

When Prism saves a recipe it writes every stage, in the order the light goes through them, with a short comment on each line saying what the number does. Here is "Warm low contrast, brighter": to get it, load the recipe above onto a photo in Prism, set Exposure to +0.5 stops and the Tone curve to Reinhard, and choose **File → Save Recipe…** with that name.

<!-- recipe: saved -->
```toml
# Prism recipe: edit settings in plain text. Prism rewrites these comments when it saves.
# What each line means: https://github.com/Peaperfish/prism/blob/main/docs/RECIPES.md
prism_recipe = 1  # recipe format version
name = "Warm low contrast, brighter"
description = "Softer contrast and a warm grade, for portraits in open shade"

[exposure]
stops = +0.5  # 1.41x the light

[matrix]
red = [1.05, 0.03, -0.01]  # output red from input R, G, B
green = [0.01, 1.0, 0.0]  # output green from input R, G, B
blue = [-0.02, -0.02, 0.94]  # output blue from input R, G, B
from = "grade.txt"  # where the numbers came from: a label, never opened

[saturation]
amount = 0.9  # 0.90x the colourfulness

[contrast]
amount = 0.85  # each stop near middle grey is 15% narrower

[curve]
shape = "reinhard"  # 18% grey becomes 15%
white = 4.0  # this scene value becomes pure white
```

The numbers in the comments are worked out by Prism from the values, with the same maths the edit uses. Prism ignores comments when it reads a recipe and writes them afresh every time it saves, so a comment you add by hand is gone after the next save. Change the value, not the comment.

A stage that is switched off is saved with `on = false` and keeps its values, so switching it back on gives the same result as before. When no colour matrix is loaded, Prism still writes the `[matrix]` section, as the identity matrix (which changes nothing), marked `# identity: no colour matrix`.

## Every setting

| Section | Key | Unit | Values | Default | Since |
|---|---|---|---|---|---|
| (top) | `prism_recipe` | format version | whole number from 1 | required | 1 |
| (top) | `name` | text | one line | none | 1 |
| (top) | `description` | text | one line | none | 1 |
| `[exposure]` | `stops` | stops | any number | required | 1 |
| `[exposure]` | `on` | switch | `true` or `false` | `true` | 1 |
| `[matrix]` | `red` | weights of input R, G, B | 3 numbers | required | 1 |
| `[matrix]` | `green` | weights of input R, G, B | 3 numbers | required | 1 |
| `[matrix]` | `blue` | weights of input R, G, B | 3 numbers | required | 1 |
| `[matrix]` | `from` | file name | one line | none | 1 |
| `[matrix]` | `on` | switch | `true` or `false` | `true` | 1 |
| `[saturation]` | `amount` | factor | 0 to 10 | required | 1 |
| `[saturation]` | `on` | switch | `true` or `false` | `true` | 1 |
| `[contrast]` | `amount` | factor | 0.1 to 10 | required | 1 |
| `[contrast]` | `on` | switch | `true` or `false` | `true` | 1 |
| `[curve]` | `shape` | curve | `none`, `gamma`, `reinhard`, `filmic` | required | 1 |
| `[curve]` | `gamma` | exponent | above 0 | 2.2 | 1 |
| `[curve]` | `white` | linear value | above 0 | 4 | 1 |
| `[curve]` | `on` | switch | `true` or `false` | `true` | 1 |

"Since" is the format version that introduced the key.

What the values mean:

- **`[exposure]` `stops`**: +1 doubles the light, −1 halves it. Prism always writes the sign.
- **`[matrix]`**: each row is one output channel as a mix of the input red, green and blue, in linear light. `red = [1.05, 0.03, -0.01]` means output red = 1.05 × red + 0.03 × green − 0.01 × blue. `from` is only a label saying which file the numbers came from: a file name, without folders, which Prism never opens.
- **`[saturation]` `amount`**: 1 changes nothing, 0 is black and white, 2 doubles each colour's distance from grey. The same number as the command line's `--saturation`.
- **`[contrast]` `amount`**: 1 changes nothing; above 1 spreads the stops near middle grey apart, below 1 brings them together. The same number as `--contrast`.
- **`[curve]` `shape`**: `"none"`, `"gamma"`, `"reinhard"` or `"filmic"`, the same curves as `--curve`. `gamma` is used only by the gamma curve and `white` only by Reinhard, so a recipe gives only the one its shape uses. The filmic curve has no setting.
- **`on`**: `false` switches the stage off. Its values are kept, but Prism skips it.

## Leaving stages out

A stage a recipe leaves out stays as it is in the photo you apply it to, and a stage the recipe has replaces it completely.

So a recipe with only `[saturation]` changes the saturation and nothing else. Within a section, the main value must be there (`stops`, all three matrix rows, `amount` or `shape`); `gamma`, `white` and `on` may be left out and then take their defaults from the table above.

A recipe never holds the photo it was made on, how you were viewing it, or the RAW decoding options (half size and the camera matrix), so applying one never makes Prism decode the file again.

## The rules of the format

A recipe is a small part of [TOML](https://toml.io), a common format for settings files, so editors that know TOML colour it nicely. Prism reads it strictly, so that a typing mistake is reported instead of quietly ignored:

- One setting per line, `key = value`. Spaces around `=` don't matter.
- Numbers are written with a dot for decimals and a digit before it: `0.5`, not `,5` or `.5`. Whole numbers such as `1` are fine anywhere. No units (`1.5`, not `1.5 stops`), no `inf` or `nan`, and no hexadecimal.
- Text goes in double quotes on one line: `name = "Warm low contrast"`. Inside, `\"` is a quote, `\\` a backslash and `\n` a new line.
- `true` and `false` are written in small letters, without quotes.
- `#` starts a comment, anywhere outside quotes.
- Sections can come in any order. Each section and each key may appear only once.
- The file is UTF-8 text, at most 64 KB. Prism writes it without a byte-order mark and with plain line feeds, and reads Windows line endings and a byte-order mark too.

## Versions

`prism_recipe = 1` says which version of the format the file is written in. When a later Prism adds a setting, it goes into a new format version, and the table above says which version each key arrived in.

A Prism that reads up to format 1 refuses a recipe that says `prism_recipe = 2` as a whole, rather than guess at settings it does not know, and asks you to update Prism. A newer Prism reads every older recipe as it was meant.

The other way round, a file must not use a key from a later version than it declares: change `prism_recipe` to that version (and use a Prism that reads it).

## When a recipe has a mistake

If anything in a recipe is wrong, Prism changes nothing and says which line is wrong and why, for up to 10 mistakes at once. Some examples, each with the message Prism gives:

A misspelt key gets a suggestion:

<!-- recipe: error line 4: unknown setting 'amout' in [saturation]. Did you mean 'amount'? -->
```toml
prism_recipe = 1

[saturation]
amout = 0.9
```

A value outside its range is refused, never quietly changed to the nearest allowed one:

<!-- recipe: error line 4: [contrast] amount must be from 0.1 to 10 (it is 15) -->
```toml
prism_recipe = 1

[contrast]
amount = 15
```

A number with a unit, or with a comma for the decimal point:

<!-- recipe: error line 4: '+1 stop' is not a number: write the number alone, without a unit -->
```toml
prism_recipe = 1

[exposure]
stops = +1 stop
```

<!-- recipe: error line 4: '0,9' is not a number: use a dot for decimals, as in 0.9 -->
```toml
prism_recipe = 1

[saturation]
amount = 0,9
```

Text without quotes:

<!-- recipe: error line 4: text needs double quotes, as in shape = "filmic" -->
```toml
prism_recipe = 1

[curve]
shape = filmic
```

A recipe from a newer Prism:

<!-- recipe: error line 1: this recipe needs recipe format 2, but this Prism reads up to format 1: update Prism to read it -->
```toml
prism_recipe = 2

[exposure]
stops = +1.0
```
