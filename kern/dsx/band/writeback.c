#include "band_frame.h"

void dsx_writeback(float *src, float *dst, int n, int upmode, int ch,
                   int nslots)
{
    int i;
    if (upmode != 2 && upmode != 4)
        return;
    for (i = 0; i < n; i++)
        dst[i * nslots + ch] = src[i];
}
