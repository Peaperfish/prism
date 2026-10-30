// The one translation unit that compiles the stb implementations.
// Every other file includes the headers without these macros and gets declarations only;
// defining them in more than one .cpp would cause duplicate-symbol link errors.

// On Windows, fopen takes file names in the ANSI code page, which can't represent most of
// Unicode. With these two macros stb converts the name from UTF-8 to UTF-16 and opens it with
// _wfopen instead (names up to 1023 UTF-16 units); on other systems they change nothing. Prism
// passes every name as UTF-8.
#define STBI_WINDOWS_UTF8
#define STBIW_WINDOWS_UTF8

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
