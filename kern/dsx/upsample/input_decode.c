#include "upsample.h"
#include "band_frame.h"
#include <math.h>
#include "mem_io.h"

void dsx_input_decode(uint8_t *mem, int c)
{
    int n = (int)dsx_ru32(mem, c + 18908);
    int pst = c + 19304;
    int step, ch, j;
    for (step = 0; step < 32; step++) {
        for (ch = 0; ch < 2; ch++) {
            int off = c + 0x4d0 + ch * DSX_SLOT + 4 * (2 * n + step);
            uint32_t pv = 1103515245u * dsx_ru32(mem, pst) + 12345u;
            dsx_wu32(mem, pst, pv);
            int r = (int)((pv >> 16) & 0x7fffu);
            dsx_wf32(mem, off, (float)(((double)r * 2.0 - 32768.0) * (1.0 / 32768.0)));
        }
    }
    for (ch = 0; ch < 2; ch++) {
        int f7 = c + 0x560 + ch * DSX_SLOT;
        float e, thr, v16, base_g;
        double lg;
        dsx_copy_f(dsx_pf(mem, f7 - 144), dsx_pf(mem, f7), 2 * n);
        dsx_copy_f(dsx_pf(mem, f7), dsx_pf(mem, f7 - 16), 2 * n);
        e = dsx_rf32(mem, f7 - 388);
        lg = e > 0.f ? log10((double)e) : -30.0;
        v16 = (float)lg * 10.f;
        base_g = e > 1.0e-12f ? dsx_rf32(mem, c + 19308) : 0.f;
        thr = dsx_rf32(mem, c + 19012);
        if (-110.0 - (double)thr <= (double)v16) {
            if (-80.0 - (double)thr > (double)v16) {
                double ex = ((double)v16 + ((double)thr + 80.0)) / 20.0;
                base_g = (float)pow(10.0, ex) * base_g;
            }
        } else {
            base_g = 0.f;
        }
        {
            float *cur = dsx_pf(mem, f7 - 144);
            int count = 2 * n + 32;
            for (j = 0; j < count; j++)
                cur[j] = (float)((double)cur[j] * (double)base_g);
        }
    }
}
