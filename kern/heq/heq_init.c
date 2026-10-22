#include <stdint.h>
#include <string.h>

#define EXPORT __declspec(dllexport)

EXPORT int dsee_heq_delays(int rate, int *delay)
{
    static const int table[][8] = {

        {32000, 67, 41, 29, 59, 43, 23, 4},
        {44100, 89, 53, 41, 83, 61, 37, 5},
        {48000, 97, 61, 43, 89, 67, 41, 6},
        {88200, 179, 113, 79, 163, 127, 73, 9},
        {96000, 193, 127, 89, 179, 131, 83, 10},
        {176400, 359, 223, 157, 331, 241, 149, 18},
        {192000, 389, 241, 173, 353, 269, 163, 20},
    };
    int i;
    for (i = 0; i < 7; i++) {
        if (table[i][0] == rate) {
            int k;
            for (k = 0; k < 7; k++)
                delay[k] = table[i][k + 1];
            return 0;
        }
    }
    return 1;
}

EXPORT int dsee_heq_gain(int bits, float *gain)
{
    uint32_t bits_f = 0;
    if (bits == 8)
        bits_f = 0x3B01F0C8u;
    else if (bits == 16)
        bits_f = 0x3701F0C8u;
    else if (bits == 20)
        bits_f = 0x34DCE65Au;
    else if (bits == 24)
        bits_f = 0x32E5F68Du;
    else if (bits == 32)
        bits_f = 0;
    else
        return 1;
    memcpy(gain, &bits_f, 4);
    return 0;
}
