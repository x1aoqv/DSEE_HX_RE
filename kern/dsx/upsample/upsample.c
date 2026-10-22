#include "upsample.h"
#include "band_frame.h"
#include "dsx.h"
#include "mem_io.h"

void dsee_upsample(const float *h, int N, int S, const float *x, float *y,
                   int a5, int niter);
void dsee_biquad(const float *coef, float gain, float *buf, const float *src,
                 float *st, int n);
void dsee_fir21(const float *c, int stride, const float *in, float *out);

__declspec(dllexport) void dsee_f80_pipeline(uint8_t *mem, float *pcm, int a3, int a4)
{
    int c = DSX_INNER;
    int nslots = (int)dsx_ru32(mem, 4);
    int up = (int)dsx_ru32(mem, c + 18892);
    int n8 = (int)dsx_ru32(mem, c + 18940);
    int n12 = (int)dsx_ru32(mem, c + 18904);
    int bypass = (int)dsx_ru32(mem, c + 19324);
    int off_up = (int)dsx_ru32(mem, c + 18996);
    int off_low = (int)dsx_ru32(mem, c + 19000);
    int gate = (int)dsx_ru32(mem, 32);
    int stride = nslots;
    int k, i;
    for (k = 0; k < nslots; k++) {
        int u = c + 956 + k * DSX_SLOT;
        int src = (int)dsx_ru32(mem, u);
        int dst = (int)dsx_ru32(mem, u - 4);
        int buf = src + 8 * n8;
        float *gathered = dsx_pf(mem, buf);
        for (i = 0; i < a3; i++)
            gathered[i] = pcm[i * nslots + k];
        if (bypass != 2 && dsx_ru32(mem, c + 18992) == 0) {
            int n = (int)dsx_ru32(mem, c + 18940);
            dsee_upsample(dsx_pf(mem, (int)dsx_ru32(mem, c + 18936)), n, up,
                          dsx_pf(mem, src), dsx_pf(mem, dst + 4 * off_up), 0, 0);
        }
        dsee_biquad(dsx_pf(mem, (int)dsx_ru32(mem, c + 18944)), dsx_rf32(mem, c + 18948),
                    dsx_pf(mem, buf + 592), dsx_pf(mem, buf), dsx_pf(mem, u + 8),
                    (int)dsx_ru32(mem, c + 18956) << 7);
        dsx_energy4(dsx_pf(mem, (int)dsx_ru32(mem, c + 18920)), dsx_pf(mem, buf + 512), dsx_pf(mem, u + 16));
        dsx_energy16(dsx_pf(mem, buf + 592), dsx_pf(mem, u + 32));
        dsx_agc_f80(mem, c, dsx_pf(mem, u + 16), dsx_pf(mem, (int)dsx_ru32(mem, u + 4)));
        dsee_fir21(dsx_pf(mem, (int)dsx_ru32(mem, u + 4)), 4, dsx_pf(mem, buf + 512),
                   dsx_pf(mem, u + 4 * (2 * n12 + 9)));
    }
    dsx_band_gain_f80(mem, c);
    dsx_input_decode(mem, c);
    for (k = 0; k < nslots; k++) {
        int base = c + 952 + k * DSX_SLOT;
        int src = (int)dsx_ru32(mem, base);
        if (bypass != 1) {
            int s = 4 * up;
            int hdr = c + 18960 + 8 * k;
            float *y = dsx_pf(mem, src + 4 * off_low);
            int n = (int)dsx_ru32(mem, hdr + 4);
            int n2 = (int)dsx_ru32(mem, hdr + 20);
            dsee_upsample(dsx_pf(mem, (int)dsx_ru32(mem, hdr)), n, s,
                          dsx_pf(mem, base + 40), y, 1, 0);
            dsee_upsample(dsx_pf(mem, (int)dsx_ru32(mem, hdr + 16)), n2, s,
                          dsx_pf(mem, base + 280), y, 1, 0);
        }
        for (i = 0; i < a4; i++)
            pcm[i * stride + k] = dsx_rf32(mem, src + 4 * i);
        if (gate != 2 && gate != 4)
            continue;
        {
            int p1 = (int)dsx_ru32(mem, base + 4);
            int hs = src + (up << 9);
            int ncopy = (dsx_ru32(mem, c + 18992) && dsx_ru32(mem, c + 18900)) ? off_up + 256 : off_up;
            dsx_copy_f(dsx_pf(mem, p1), dsx_pf(mem, p1 + 512), 2 * n8);
            dsx_copy_f(dsx_pf(mem, p1 + 8 * n8 + 512), dsx_pf(mem, p1 + 8 * n8 + 1024), 20);
            dsx_copy_f(dsx_pf(mem, base + 40), dsx_pf(mem, base + 168), 2 * n12);
            dsx_copy_f(dsx_pf(mem, src), dsx_pf(mem, hs), ncopy);
        }
    }
}
