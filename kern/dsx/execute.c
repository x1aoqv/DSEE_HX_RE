#include "dsx.h"
#include "mem_io.h"

__declspec(dllexport) void dsee_dsp(uint8_t *mem, float *pcm, int a3, int a4)
{
    if (dsx_ru32(mem, 76) != 0)
        dsee_f76_frame(mem, pcm, a3, a3, 0);
    if (dsx_ru32(mem, 80) != 0)
        dsee_f80_pipeline(mem, pcm, a3, a4);
}
