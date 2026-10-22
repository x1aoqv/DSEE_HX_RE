#include "upsample.h"
#include <math.h>
#include "mem_io.h"

void dsx_agc_f80(uint8_t *mem, int c, float *g, float *out)
{
    float a4 = dsx_rf32(mem, c + 19032);
    float a5 = dsx_rf32(mem, c + 4456);
    int ref = (int)dsx_ru32(mem, c + 19300);
    const float *common = dsx_pf(mem, (int)dsx_ru32(mem, c + 18920));
    float w[4];
    int i, k, j;
    for (i = 0; i < 5; i++) {
        double v = (double)g[i] * 0.0625;
        if (v >= 1.0e-12) {
            if (v > 1.0)
                v = 1.0;
        } else {
            v = 1.0e-12;
        }
        g[i] = (float)v;
    }
    w[0] = w[1] = w[2] = w[3] = 0.f;
    for (i = 0; i < 2; i++) {
        double q = g[i] != 0.f ? sqrt((double)g[ref] / (double)g[i]) : 0.0;
        double ww = (double)(float)q * (double)a4;
        if (ww > (double)a5)
            ww = (double)a5;
        w[i] = (float)ww;
    }
    for (k = 0; k < 11; k++) {
        double v = 0.0;
        for (j = 0; j < 4; j++)
            v = (double)(float)(v + (double)common[4 * k + j] * (double)w[j]);
        out[k] = (float)v;
    }
}
