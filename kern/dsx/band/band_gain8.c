#include "band_frame.h"
#include <math.h>
#include "mem_io.h"

void dsx_band_gain8(uint8_t *mem, uint32_t ctx, uint32_t meta, uint32_t coefs,
                 int flag, uint32_t base)
{
    float *a1 = dsx_F(mem, ctx);
    float *a5 = dsx_F(mem, base);
    float bands[9];
    int i, k;
    for (k = 0; k < 9; k++)
        bands[k] = 0.0f;
    for (i = 0; i < 5; i++) {
        float v = x87_mul(a1[184 + i], 16.f);
        if (v >= 1.0e-12f) {
            if (v > 16.f)
                v = 16.f;
        } else {
            v = 1.0e-12f;
        }
        a1[184 + i] = x87_mul((float)log10((double)v), 10.f);
    }
    for (i = 0; i < 4; i++) {
        float v = x87_mul(a1[224 + i], 16.f);
        if (v >= 1.0e-12f) {
            if (v > 16.f)
                v = 16.f;
        } else {
            v = 1.0e-12f;
        }
        a1[224 + i] = x87_mul((float)log10((double)v), 10.f);
    }

    if (flag != 0) {
        float g = a1[188];
        for (i = 0; i < 8; i++)
            bands[i] = x87_add(x87_mul(g, a5[4650 + 2 * i]), a5[4649 + 2 * i]);
    } else {
        uint32_t v11 = base + 18436;
        float *p = dsx_F(mem, v11);
        k = 0;
        bands[0] = 0.0f;
        while (k < 8) {
            float v13 = p[0];
            float *v14 = p + 1;
            float v15, acc;
            bands[k] = v13;
            k++;
            v15 = v14[0];
            p = v14 + 4;
            acc = x87_mac(bands[k - 1], v15, a1[184]);
            acc = x87_mac(acc, p[-3], a1[185]);
            acc = x87_mac(acc, p[-2], a1[186]);
            bands[k - 1] = x87_mac(acc, p[-1], a1[187]);
        }
    }

    {
        uint32_t v16 = *(uint32_t *)(mem + base + 4496);
        float lim = x87_sub(-2.f, a5[4753]);
        float off = x87_add(a5[4757], a5[1126]);
        float *v19 = &bands[1];
        float *v20 = a1 + 225;
        int v18 = 2;
        do {

            if (v16 == 1)
                v19[-1] = x87_add(off, v19[-1]);
            if (v19[-1] > lim)
                v19[-1] = lim;
            if (*(uint32_t *)(mem + base + 4460) != 0 && v18 - 2 < 4) {
                float g = x87_add(v20[-1], a5[1127]);
                if (v19[-1] < g)
                    v19[-1] = g;
            }
            if (v16 != 0) {
                if (v16 == 1)
                    v19[0] = x87_add(off, v19[0]);
            } else {
                v19[-1] = x87_add(off, v19[-1]);
            }
            if (v19[0] > lim)
                v19[0] = lim;
            if (*(uint32_t *)(mem + base + 4460) != 0 && v18 - 1 < 4) {
                float g = x87_add(a5[1127], v20[0]);
                if (v19[0] < g)
                    v19[0] = g;
            }
            if (v16 != 0) {
                if (v16 == 1)
                    v19[1] = x87_add(off, v19[1]);
            } else {
                v19[0] = x87_add(off, v19[0]);
            }
            if (v19[1] > lim)
                v19[1] = lim;
            if (*(uint32_t *)(mem + base + 4460) != 0 && v18 < 4) {
                float g = x87_add(v20[1], a5[1127]);
                if (v19[1] < g)
                    v19[1] = g;
            }
            if (v16 != 0) {
                if (v16 == 1)
                    v19[2] = x87_add(off, v19[2]);
            } else {
                v19[1] = x87_add(off, v19[1]);
            }
            if (v19[2] > lim)
                v19[2] = lim;
            if (*(uint32_t *)(mem + base + 4460) != 0 && v18 + 1 < 4) {
                float g = x87_add(v20[2], a5[1127]);
                if (v19[2] < g)
                    v19[2] = g;
            }
            if (v16 == 0)
                v19[2] = x87_add(off, v19[2]);
            v18 += 4;
            v20 += 4;
            v19 += 4;
        } while (v18 - 2 < 8);
    }

    {
        int n = (int)*(uint32_t *)(mem + meta + 12);
        for (i = 0; i < n; i++) {
            uint32_t idx = *(uint32_t *)(mem + meta + 20 + 4 * i);
            float g = (float)pow(
                10.0, (double)x87_sub(bands[i], a1[idx + 184]) / 20.0);
            a1[228 + i] = g;
            if (a5[1113] < g)
                a1[228 + i] = a5[1113];
        }
        for (i = n; i < 8; i++)
            a1[228 + i] = 0.0f;
    }

    {
        float *out = dsx_F(mem, *(uint32_t *)(mem + coefs + 4));
        float *src = dsx_F(mem, *(uint32_t *)(mem + coefs));
        int cnt = (int)*(uint32_t *)(mem + meta + 16);
        int total = (int)*(uint32_t *)(mem + meta + 12);
        int v52 = 0, v53 = 0;
        do {
            float acc0 = 0.0f, acc1 = 0.0f;
            for (i = 0; i < cnt; i++)
                acc0 = x87_mac(acc0, src[v52 + i], a1[228 + i]);
            out[0] = acc0;
            for (i = 0; i < total - cnt; i++)
                acc1 = x87_mac(acc1, src[v52 + i], a1[228 + cnt + i]);
            out[1] = acc1;
            v52 += 4;
            v53 += 16;
            out += 2;
        } while (v53 < 336);
    }
}
