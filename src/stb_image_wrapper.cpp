// Despite the filename, this is now the single implementation TU for
// both stb_image (decode) and stb_image_write (encode, used by the
// Input History glyph mapping creator to convert/resize a user-picked
// image into a PNG - see input_history_glyphs.cpp) - kept together
// rather than adding a second nearly-empty wrapper file, since both
// need exactly the same "exactly one TU defines
// STB_*_IMPLEMENTATION" treatment.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"