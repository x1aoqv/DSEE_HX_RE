#include <stdint.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

#define RING 1589

static const double K0 = 0.6484;
static const double K1 = 0.6016;
static const double K2 = 0.5391;

static float flush(double x)
{
    float v = (float)x;
    const float eps = 0x1p-10f;
    if ((double)v > -(double)eps && (double)v < (double)eps)
        return 0.f;
    return v;
}

static void ring_write(float *ring, int idx, float value)
{
    int mirror = idx >= RING ? idx - RING : idx + RING;
    ring[idx] = value;
    ring[mirror] = value;
}

static int32_t channel(float *ring, float *hist, int lag, int hidx,
                       int s0, int s1, int s2, int s3, int s4, int s5,
                       int dry_at, int32_t sample, double gain)
{
    double xs = (double)sample;
    float x32 = (float)xs;
    double v68, v57, v59, v42, v62, v64, v66;
    float v20, v24, v27, v30, v33, v35;
    float mixed;
    double d;

    hist[hidx] = x32;
    hist[hidx + 21] = x32;

    v68 = ring[s0];
    v20 = flush(xs - v68 * K0);
    ring_write(ring, lag, v20);
    v57 = (float)(v68 + (double)v20 * K0);

    v68 = ring[s1];
    v24 = flush((double)v57 - v68 * K1);
    ring_write(ring, s0, v24);
    v59 = (float)(v68 + (double)v24 * K1);

    v68 = ring[s2];
    v27 = flush((double)v59 - v68 * K2);
    ring_write(ring, s1, v27);
    v42 = (float)(v68 + (double)v27 * K2);

    v68 = ring[s3];
    v30 = flush(v68 * K0 + xs);
    ring_write(ring, s2, v30);
    v62 = (float)(v68 - (double)v30 * K0);

    v68 = ring[s4];
    v33 = flush(v68 * K1 + (double)v62);
    ring_write(ring, s3, v33);
    v64 = (float)(v68 - (double)v33 * K1);

    v68 = ring[s5];
    v35 = flush(v68 * K2 + (double)v64);
    ring_write(ring, s4, v35);
    v66 = (float)(v68 - (double)v35 * K2);

    mixed = (float)(((double)v66 + (double)v42) * gain + (double)hist[dry_at]);
    d = mixed;
    if (d > 2147483647.0)
        return 2147483647;
    if (d < -2147483648.0)
        return (int32_t)(-2147483647 - 1);
    return (int32_t)d;
}

EXPORT void dsee_heq(float *ring0, float *ring1, float *hist0, float *hist1,
                     int *lag, int *hidx, const int *delay, double gain,
                     const int32_t *in, int32_t *out, int nframe)
{
    int n;
    int d0 = delay[0], d1 = delay[1], d2 = delay[2], d3 = delay[3];
    int d4 = delay[4], d5 = delay[5], d6 = delay[6];

    for (n = 0; n < nframe; n++) {
        int base = *lag + d0;
        int s0 = base;
        int s1 = base + d1;
        int s2 = s1 + d2;
        int s3 = s2 + d3;
        int s4 = s3 + d4;
        int s5 = s4 + d5;
        int dry = *hidx + d6;

        out[n * 2] = channel(ring0, hist0, *lag, *hidx, s0, s1, s2, s3, s4, s5,
                             dry, in[n * 2], gain);

        out[n * 2 + 1] = channel(ring1, hist1, *lag, *hidx, s0, s1, s2, s3, s4, s5,
                                 dry, in[n * 2 + 1], gain);

        *lag = (*lag <= 0) ? (RING - 1) : (*lag - 1);
        *hidx = (*hidx <= 0) ? 20 : (*hidx - 1);
    }
}
