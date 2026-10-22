#include "upsample.h"
#include <math.h>
#include <string.h>
#include "mem_io.h"

void dsx_band_gain_f80(uint8_t *mem, int c)
{
    float *mid = dsx_pf(mem, c + 19100);
    float *taps = dsx_pf(mem, c + 19036);
    float v55[8];
    int ref = (int)dsx_ru32(mem, c + 19300);
    int bypass = (int)dsx_ru32(mem, c + 19324);
    const float *src = dsx_pf(mem, (int)dsx_ru32(mem, c + 18924));
    const float *win = dsx_pf(mem, (int)dsx_ru32(mem, c + 18928));
    int klen = (int)dsx_ru32(mem, c + 18932);
    int result = klen + 49;
    int outs[2];
    int frame, i, k, ti;
    memset(mid, 0, 50 * sizeof(float));
    for (frame = 0; frame < 2; frame++) {
        int base = c + 4 * (ref + 241) + frame * DSX_SLOT;
        float db[3];
        int p = 0;
        for (i = 0; i < 3; i++) {
            float e = dsx_rf32(mem, base + 4 * i);
            double lg = e > 0.f ? log10((double)e) : -30.0;
            float lgf = (float)lg;

            db[i] = lgf * 10.f + 120.f;
        }
        for (k = 0; k < 4; k++) {
            float v = taps[p];
            v = (float)((double)v + (double)taps[p + 1] * (double)db[0]);
            v = (float)((double)v + (double)taps[p + 2] * (double)db[1]);
            v = (float)((double)v + (double)taps[p + 3] * (double)db[2]);
            v55[frame + 2 * k] = (float)pow(10.0, ((double)v - (double)db[2]) / 20.0);
            p += 4;
        }
    }
    for (frame = 0; frame < 2; frame++) {
        int sp = 0;
        int d = 2 + frame * 25;
        for (k = 0; k < 4; k++) {
            int v14 = d;
            float g = v55[frame + 2 * k];
            int m;
            for (m = 0; m < 5; m++) {
                int t;
                if (bypass == 2) {
                    if (k == 0) {
                        for (t = 0; t < 5; t++) {
                            mid[v14 - 2 + t] = (float)((double)mid[v14 - 2 + t] + (double)src[sp]);
                            sp++;
                        }
                    } else {
                        sp += 5;
                    }
                } else {
                    for (t = 0; t < 5; t++) {
                        mid[v14 - 2 + t] = (float)((double)g * (double)src[sp] + (double)mid[v14 - 2 + t]);
                        sp++;
                    }
                }
                v14 += 5;
            }
        }
    }
    outs[0] = (int)dsx_ru32(mem, c + 18960);
    outs[1] = (int)dsx_ru32(mem, c + 18968);
    for (ti = 0; ti < 2; ti++) {
        const float *cv = mid + ti * 25;
        float *out = dsx_pf(mem, outs[ti]);
        int oi;
        for (oi = 0; oi < result - 24; oi++) {
            int cen = oi + 24;
            float acc = (float)((double)win[cen] * (double)cv[0]);
            int j;
            for (j = 1; j < 25; j++) {
                float t = (float)((double)win[cen - j] + (double)win[cen + j]);
                acc = (float)((double)t * (double)cv[j] + (double)acc);
            }
            out[oi] = acc;
        }
    }
}
