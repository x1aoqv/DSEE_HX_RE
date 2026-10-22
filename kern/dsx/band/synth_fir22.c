#include "band_frame.h"
#include "mem_io.h"

void dsx_synth_fir22(uint8_t *mem, uint32_t coef, uint32_t meta, uint32_t src,
                  uint32_t dst)
{
    float *c = dsx_F(mem, coef);
    float *mx = dsx_F(mem, *(uint32_t *)(mem + meta));
    int nch = (int)*(uint32_t *)(mem + meta + 4);
    int v5 = 20;
    for (;;) {
        float *v8 = dsx_F(mem, src + 4 * v5);
        int j;
        for (j = 0; j < 2 * nch; j++) {
            float *v9 = v8 - 3;
            float *v10 = c + 4;
            float *v11 = v8 + 1;
            float v25 = x87_mul(v8[0], c[0]);
            float v15 = x87_mul(v8[0], c[1]);
            int t;
            for (t = 0; t < 2; t++) {
                float s;
                s = x87_add(v9[2], v11[0]);
                v25 = x87_mac(v25, v10[-2], s);
                v15 = x87_mac(v15, v10[-1], s);
                s = x87_add(v11[1], v9[1]);
                v25 = x87_mac(v25, v10[0], s);
                v15 = x87_mac(v15, v10[1], s);
                s = x87_add(v11[2], v9[0]);
                v25 = x87_mac(v25, v10[2], s);
                v15 = x87_mac(v15, v10[3], s);
                s = x87_add(v11[3], v9[-1]);
                v25 = x87_mac(v25, v10[4], s);
                v15 = x87_mac(v15, v10[5], s);
                s = x87_add(v11[4], v9[-2]);
                v25 = x87_mac(v25, v10[6], s);
                v15 = x87_mac(v15, v10[7], s);
                s = x87_add(v11[5], v9[-3]);
                v25 = x87_mac(v25, v10[8], s);
                v15 = x87_mac(v15, v10[9], s);
                v11 += 10;
                v9 -= 10;
                v10 += 20;
                s = x87_add(v11[-4], v9[6]);
                v25 = x87_mac(v25, v10[-10], s);
                v15 = x87_mac(v15, v10[-9], s);
                s = x87_add(v11[-3], v9[5]);
                v25 = x87_mac(v25, v10[-8], s);
                v15 = x87_mac(v15, v10[-7], s);
                s = x87_add(v11[-2], v9[4]);
                v25 = x87_mac(v25, v10[-6], s);
                v15 = x87_mac(v15, v10[-5], s);
                s = x87_add(v11[-1], v9[3]);
                v25 = x87_mac(v25, v10[-4], s);
                v15 = x87_mac(v15, v10[-3], s);
            }
            dsx_F(mem, dst + 4 * v5 - 80)[j] =
                x87_add(x87_mul(mx[j + 2 * nch], v15), x87_mul(mx[j], v25));
            v8 += 1;
        }
        if (v5 + 2 * nch >= 148)
            break;
        v5 += 2 * nch;
    }
}
