#include "band_frame.h"

void dsx_gather(const float *src, float *dst, int n, int ch, int nslots)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] = src[i * nslots + ch];
}
