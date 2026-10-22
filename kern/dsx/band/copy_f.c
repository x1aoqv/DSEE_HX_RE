#include "band_frame.h"

void dsx_copy_f(float *dst, const float *src, int n)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}
