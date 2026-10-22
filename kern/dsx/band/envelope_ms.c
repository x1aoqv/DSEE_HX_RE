#include "band_frame.h"
#include <math.h>
#include "mem_io.h"

void dsx_envelope_ms(uint8_t *mem, int nslots, int nframe, uint32_t coef,
                uint32_t state, float a5, const uint32_t *a6,
                const uint32_t *a7)
{
    float *st = dsx_F(mem, state);
    float *cf = dsx_F(mem, coef);
    int ch, i;
    for (ch = 0; ch < nslots; ch++) {
        float *in = dsx_F(mem, a6[ch]);
        float *v10 = st + 3 + ch;
        float *v11 = st + 8 + ch * 5;
        for (i = 0; i < nframe; i++) {
            float neu, dev;
            v11[3] = v11[2];
            v11[2] = v11[1];
            v11[1] = v11[0];
            v11[0] = v11[-1];
            neu = in[i];
            v11[-1] = neu;

            dev = x87_diff25(v11[1], neu, v11[3]);
            v10[-2] = x87_env(v10[-2], dev, st[29]);
            v10[0] = x87_env(v10[0], v11[1], st[31]);
        }
    }

    st[27] = 100000.0f;
    for (ch = 0; ch < nslots; ch++) {
        float *v18 = st + 1 + ch;
        float v84 = (float)log10((double)v18[2]);
        float v85 = x87_mul(v84, 10.f);
        float v86 = x87_add(v85, a5);
        float v48 = x87_sub(0.f, x87_add(v86, cf[20]));
        float v87, v46, v88, v19;
        float v51, v52;
        if (v48 < 0.f)
            v48 = 0.f;
        v87 = x87_add(x87_mul(v86, cf[7]), cf[8]);
        v51 = (float)log10((double)v18[0]);
        v52 = x87_mul(v51, 10.f);
        v46 = x87_add(v52, a5);
        if (cf[9] > v46)
            v46 = cf[9];
        v88 = x87_sub(v87, v46);
        v19 = v88;
        if (cf[10] < v88)
            v19 = cf[10];
        if (v48 < v19)
            v19 = v48;
        if (st[27] > v19)
            st[27] = v19;
    }
    {
        float g = st[27];
        if (g < 0.f)
            g = 0.f;
        st[27] = (float)pow(10.0, (double)g / 20.0);
    }

    for (ch = 0; ch < nslots; ch++) {
        float *src = dsx_F(mem, a6[ch]);
        float *dst = dsx_F(mem, a7[ch]);
        float *v22 = st + 18 + ch * 5;
        for (i = 0; i < nframe; i++) {
            float neu, h1, h3;
            v22[3] = v22[2];
            v22[2] = v22[1];
            v22[1] = v22[0];
            v22[0] = v22[-1];
            neu = src[i];
            v22[-1] = neu;

            h1 = x87_mul(v22[1], 0.5f);
            h3 = x87_addmul(neu, v22[3], 0.25f);
            dst[i] = x87_midside(h1, h3, st[27]);
        }
    }
    {
        uint32_t cnt = *(uint32_t *)(mem + state);
        uint32_t lim = *(uint32_t *)(mem + state + 132);
        if (cnt > lim) {
            st[29] = st[28];
            st[31] = st[30];
        } else {
            *(uint32_t *)(mem + state) = cnt + (uint32_t)nframe;
        }
    }
}
