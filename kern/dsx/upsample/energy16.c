#include "upsample.h"

void dsx_energy16(const float *src, float *out)
{
    float acc = 0.f;
    int i;
    for (i = 0; i < 0x80; i += 8) {
        float v = src[i];
        acc = (float)((double)v * (double)v + (double)acc);
    }
    out[0] = acc;
}
