#include "band_frame.h"
#include <string.h>

void dsx_clear_f(float *p, int n)
{
    if (n > 0)
        memset(p, 0, (size_t)n * sizeof(float));
}
