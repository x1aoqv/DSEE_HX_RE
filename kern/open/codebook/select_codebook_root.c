#include "codebook.h"

extern const CbEnt root_A[18];
extern const CbEnt root_B[18];
extern const CbEnt root_C[18];
extern const CbEnt root_D[18];
extern const CbEnt root_E[18];
extern const CbEnt root_F[18];

const CbEnt *dsx_select_codebook_root(int channels, int vbr, int content_idx)
{
    const CbEnt *root = 0;
    if ((unsigned)(content_idx - 4) <= 3u) {
        if (channels == 1)
            root = vbr ? root_E : root_A;
        else if (channels == 2)
            root = vbr ? root_F : root_B;
    } else if (content_idx == 8) {
        if (channels == 1)
            root = vbr ? root_E : root_C;
        else if (channels == 2)
            root = vbr ? root_F : root_D;
    }
    return root;
}
