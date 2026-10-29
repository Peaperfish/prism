// The one translation unit that compiles the stb implementations.
// Every other file includes the headers without these macros and gets declarations only;
// defining them in more than one .cpp would cause duplicate-symbol link errors.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
