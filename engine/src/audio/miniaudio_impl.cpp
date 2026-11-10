// Single translation unit that compiles the miniaudio implementation.
#ifdef AETHER_AUDIO_MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "miniaudio.h"
#endif
