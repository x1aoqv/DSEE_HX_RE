#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dsx.h"

#define EXPORT __declspec(dllexport)

void dsee_heq(float *ring0, float *ring1, float *hist0, float *hist1,
              int *lag, int *hidx, const int *delay, double gain,
              const int32_t *in, int32_t *out, int nframe);
void dsee_frame(uint8_t *mem, const int32_t *in, int n_in, int n_out,
                int a3, int a4, float *pcm);

static int process_i32(uint8_t *mem, float *ring0, float *ring1,
                       float *hist0, float *hist1, int *lag, int *hidx,
                       const int *delay, double gain, const int32_t *pcm,
                       int hops, int n_in, int n_out, int a3, int a4,
                       float *out)
{
    int h;
    int32_t *heq;
    float *frame;
    if (hops < 0 || n_in < 2 || (n_in & 1) || n_out < 1)
        return 1;
    heq = (int32_t *)malloc((size_t)n_in * sizeof(int32_t));
    frame = (float *)malloc((size_t)n_out * sizeof(float));
    if (!heq || !frame) {
        free(heq);
        free(frame);
        return 2;
    }
    for (h = 0; h < hops; h++) {
        dsee_heq(ring0, ring1, hist0, hist1, lag, hidx, delay, gain,
                 pcm + (size_t)h * n_in, heq, n_in / 2);
        dsee_frame(mem, heq, n_in, n_out, a3, a4, frame);
        memcpy(out + (size_t)h * n_out, frame, (size_t)n_out * sizeof(float));
    }
    free(heq);
    free(frame);
    return 0;
}

EXPORT int dsee_process_i32(uint8_t *mem, float *ring0, float *ring1,
                            float *hist0, float *hist1, int *lag, int *hidx,
                            const int *delay, double gain, const int32_t *pcm,
                            int hops, int n_in, int n_out, int a3, int a4,
                            float *out)
{
    return process_i32(mem, ring0, ring1, hist0, hist1, lag, hidx, delay, gain,
                       pcm, hops, n_in, n_out, a3, a4, out);
}

EXPORT void dsee_frame(uint8_t *mem, const int32_t *in, int n_in, int n_out,
                       int a3, int a4, float *pcm)
{
    int i;
    float hi;
    uint32_t hib = 0x3f7fffffu;
    memcpy(&hi, &hib, 4);
    for (i = 0; i < n_out; i++)
        pcm[i] = 0.f;
    for (i = 0; i < n_in; i++)
        pcm[i] = (float)((double)in[i] * 0x1p-31);
    dsee_dsp(mem, pcm, a3, a4);
    for (i = 0; i < n_out; i++) {
        float v = pcm[i];
        int iv;
        if (v >= hi)
            iv = 2147483520;
        else if (v <= -1.f)
            iv = -2147483647 - 1;
        else
            iv = (int)((double)v * 2147483648.0);
        pcm[i] = (float)((double)iv / 2147483648.0);
    }
}
