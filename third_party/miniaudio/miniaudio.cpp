// The miniaudio implementation (third_party/miniaudio, public domain or
// MIT-0). Built on its own, without the project's warning flags. Only the
// device I/O is built (the MA_NO_* definitions in CMakeLists.txt): decoding
// is FFmpeg's.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
