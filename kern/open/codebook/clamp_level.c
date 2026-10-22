#include "codebook.h"

void dsx_clamp_level(int *level, unsigned codec, int content_idx)
{
    switch (content_idx) {
    case 0:
    case 1:
    case 2:
    case 3:
        *level = 16;
        break;
    case 4:
    case 5:
        if (codec < 2)
            *level = 16;
        else if (*level > 8 && *level < 16)
            *level = 8;
        break;
    case 6:
        if (codec < 2)
            *level = 16;
        else if (*level > 11 && *level < 16)
            *level = 11;
        break;
    default:
        break;
    }
}
