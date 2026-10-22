#include "upsample.h"

void dsx_energy4(const float *c, const float *buf, float *out)
{
    int v3 = 12;
    int round, j;
    float e0 = 0.f, e1 = 0.f, e2 = 0.f, e3 = 0.f, es = 0.f;
    for (round = 0; round < 16; round++) {
        float s = buf[v3 - 2];
        float a0 = (float)((double)c[0] * (double)s);
        float a1 = (float)((double)c[1] * (double)s);
        float a2 = (float)((double)c[2] * (double)s);
        float a3 = (float)((double)c[3] * (double)s);
        for (j = 1; j < 11; j++) {
            s = (float)((double)buf[v3 - 2 - j] + (double)buf[v3 + j - 2]);
            a0 = (float)((double)c[4 * j + 0] * (double)s + (double)a0);
            a1 = (float)((double)c[4 * j + 1] * (double)s + (double)a1);
            a2 = (float)((double)c[4 * j + 2] * (double)s + (double)a2);
            a3 = (float)((double)c[4 * j + 3] * (double)s + (double)a3);
        }
        e0 = (float)((double)a0 * (double)a0 + (double)e0);
        e1 = (float)((double)a1 * (double)a1 + (double)e1);
        e2 = (float)((double)a2 * (double)a2 + (double)e2);
        e3 = (float)((double)a3 * (double)a3 + (double)e3);
        {
            float tf = (float)((double)a0 + (double)a1 + (double)a2 + (double)a3);
            es = (float)((double)tf * (double)tf + (double)es);
        }
        v3 += 8;
    }
    out[0] = e0;
    out[1] = e1;
    out[2] = e2;
    out[3] = e3;
    out[4] = es;
}
