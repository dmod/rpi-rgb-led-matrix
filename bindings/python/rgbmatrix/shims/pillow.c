/*
 * Shim to access Pillow's internal image buffer.
 * 
 * We try to use Pillow's official Imaging.h if available (from python3-pil 
 * development packages), otherwise fall back to a minimal struct definition.
 */
#include "pillow.h"

/* Try to use official Pillow header if available */
#if __has_include("Imaging.h")
#include "Imaging.h"
#else
/* Minimal definition of Pillow's ImagingMemoryInstance structure.
 * We only need the image32 field for RGBA access.
 * Based on Pillow's Imaging.h - only the fields up to image32 matter.
 * This struct layout has been stable in Pillow for many years. */
struct ImagingMemoryInstance {
    char mode[7];
    int type;
    int depth;
    int bands;
    int xsize;
    int ysize;
    void *palette;
    unsigned char **image8;
    int **image32;
};
#endif

int** get_image32(void* im) {
    struct ImagingMemoryInstance* image = (struct ImagingMemoryInstance*) im;
    return image->image32;
}
