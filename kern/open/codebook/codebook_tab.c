#include "codebook.h"

static const CbPair cb_A_1[] = {{16, 128}, {12, 96}, {11, 64}, {10, 48}, {8, 32}};
static const CbPair cb_A_2[] = {{13, 160}, {12, 112}, {11, 80}, {10, 64}, {9, 48}, {8, 40}};
static const CbPair cb_A_3[] = {{14, 128}, {13, 96}, {12, 80}, {11, 56}, {9, 40}, {8, 24}};
static const CbPair cb_A_4[] = {{8, 16}};
static const CbPair cb_A_5[] = {{11, 16}};
static const CbPair cb_A_6[] = {{12, 96}, {11, 80}, {10, 64}, {13, 32}, {10, 16}};
static const CbPair cb_A_7[] = {{14, 192}, {13, 144}, {12, 120}, {11, 84}, {9, 60}, {8, 24}};
static const CbPair cb_A_8[] = {{14, 128}, {12, 112}, {11, 96}, {10, 80}, {9, 64}};
static const CbPair cb_A_9[] = {{14, 128}, {12, 112}, {11, 96}, {10, 64}, {9, 56}};
static const CbPair cb_A_10[] = {{14, 128}, {13, 96}, {12, 80}, {11, 56}, {9, 40}, {8, 24}};
static const CbPair cb_A_11[] = {{16, 112}, {14, 96}, {12, 80}, {11, 64}, {10, 44}};
static const CbPair cb_A_12[] = {{16, 16}};
static const CbPair cb_A_13[] = {{16, 16}};
static const CbPair cb_A_14[] = {{11, 44}, {9, 32}};
static const CbPair cb_A_15[] = {{16, 16}};
static const CbPair cb_A_16[] = {{16, 16}};
static const CbPair cb_A_17[] = {{16, 16}};

static const CbPair cb_B_0[] = {{10, 104}, {9, 64}};
static const CbPair cb_B_1[] = {{16, 320}, {14, 256}, {13, 192}, {12, 160}, {11, 128}, {10, 48}};
static const CbPair cb_B_2[] = {{13, 320}, {12, 224}, {11, 192}, {10, 128}, {9, 96}, {8, 64}};
static const CbPair cb_B_3[] = {{14, 256}, {13, 192}, {11, 160}, {11, 80}, {8, 48}};
static const CbPair cb_B_4[] = {{11, 192}, {10, 128}, {9, 64}, {8, 48}};
static const CbPair cb_B_5[] = {{11, 32}};
static const CbPair cb_B_6[] = {{12, 192}, {11, 160}, {10, 128}, {13, 64}, {10, 32}};
static const CbPair cb_B_7[] = {{14, 384}, {13, 288}, {11, 240}, {10, 120}, {8, 48}};
static const CbPair cb_B_8[] = {{14, 192}, {11, 160}, {10, 128}, {9, 96}};
static const CbPair cb_B_9[] = {{14, 192}, {13, 176}, {11, 160}, {10, 96}};
static const CbPair cb_B_10[] = {{14, 256}, {13, 192}, {11, 160}, {10, 80}, {8, 48}};
static const CbPair cb_B_11[] = {{16, 192}, {14, 160}, {13, 128}, {12, 112}, {11, 64}, {9, 44}};
static const CbPair cb_B_12[] = {{16, 16}};
static const CbPair cb_B_13[] = {{16, 16}};
static const CbPair cb_B_14[] = {{11, 44}, {9, 32}};
static const CbPair cb_B_15[] = {{16, 16}};
static const CbPair cb_B_16[] = {{16, 16}};
static const CbPair cb_B_17[] = {{16, 16}};

static const CbPair cb_C_1[] = {{16, 128}, {12, 96}, {10, 64}, {9, 48}, {8, 32}};
static const CbPair cb_C_2[] = {{13, 160}, {12, 112}, {11, 80}, {10, 64}, {9, 48}, {8, 40}};
static const CbPair cb_C_3[] = {{14, 128}, {13, 96}, {12, 80}, {11, 56}, {9, 40}, {8, 24}};
static const CbPair cb_C_4[] = {{8, 16}};
static const CbPair cb_C_5[] = {{11, 16}};
static const CbPair cb_C_6[] = {{12, 96}, {11, 80}, {10, 64}, {13, 32}, {10, 16}};
static const CbPair cb_C_7[] = {{14, 192}, {13, 144}, {12, 120}, {11, 84}, {9, 60}, {8, 24}};
static const CbPair cb_C_8[] = {{14, 128}, {12, 112}, {11, 96}, {10, 80}, {9, 64}};
static const CbPair cb_C_9[] = {{14, 128}, {12, 112}, {11, 96}, {10, 64}, {9, 56}};
static const CbPair cb_C_10[] = {{14, 128}, {13, 96}, {12, 80}, {11, 56}, {9, 40}, {8, 24}};
static const CbPair cb_C_11[] = {{16, 112}, {14, 96}, {12, 80}, {11, 64}, {10, 44}};
static const CbPair cb_C_12[] = {{16, 16}};
static const CbPair cb_C_13[] = {{16, 16}};
static const CbPair cb_C_14[] = {{11, 44}, {9, 32}};
static const CbPair cb_C_15[] = {{16, 16}};
static const CbPair cb_C_16[] = {{16, 16}};
static const CbPair cb_C_17[] = {{16, 16}};

static const CbPair cb_D_1[] = {{16, 320}, {15, 256}, {12, 160}, {10, 64}};
static const CbPair cb_D_2[] = {{13, 320}, {12, 224}, {11, 192}, {10, 128}, {9, 96}, {8, 64}};
static const CbPair cb_D_3[] = {{14, 256}, {13, 192}, {11, 160}, {11, 80}, {8, 48}};
static const CbPair cb_D_4[] = {{11, 192}, {10, 128}, {9, 64}, {8, 48}};
static const CbPair cb_D_5[] = {{11, 32}};
static const CbPair cb_D_6[] = {{12, 192}, {11, 160}, {10, 128}, {13, 64}, {10, 32}};
static const CbPair cb_D_7[] = {{14, 384}, {13, 288}, {11, 240}, {10, 120}, {8, 48}};
static const CbPair cb_D_8[] = {{14, 192}, {11, 160}, {10, 128}, {9, 96}};
static const CbPair cb_D_9[] = {{14, 192}, {13, 176}, {11, 160}, {10, 96}};
static const CbPair cb_D_10[] = {{14, 256}, {13, 192}, {11, 160}, {10, 80}, {8, 48}};
static const CbPair cb_D_11[] = {{16, 192}, {14, 160}, {13, 128}, {12, 112}, {11, 64}, {9, 44}};
static const CbPair cb_D_12[] = {{16, 16}};
static const CbPair cb_D_13[] = {{16, 16}};
static const CbPair cb_D_14[] = {{11, 44}, {9, 32}};
static const CbPair cb_D_15[] = {{16, 16}};
static const CbPair cb_D_16[] = {{16, 16}};
static const CbPair cb_D_17[] = {{16, 16}};

static const CbPair cb_E_2[] = {{13, 128}, {12, 112}, {11, 64}, {10, 56}, {9, 48}, {8, 40}};
static const CbPair cb_E_3[] = {{14, 128}, {13, 96}, {12, 80}, {11, 56}, {9, 40}, {8, 24}};
static const CbPair cb_E_4[] = {{8, 16}};
static const CbPair cb_E_5[] = {{11, 16}};
static const CbPair cb_E_6[] = {{12, 96}, {11, 80}, {10, 64}, {13, 32}, {10, 16}};
static const CbPair cb_E_7[] = {{14, 192}, {13, 144}, {12, 120}, {11, 84}, {9, 60}, {8, 24}};
static const CbPair cb_E_8[] = {{14, 128}, {12, 112}, {11, 96}, {10, 80}, {9, 64}};
static const CbPair cb_E_9[] = {{14, 128}, {12, 112}, {11, 96}, {10, 64}, {9, 56}};
static const CbPair cb_E_10[] = {{14, 128}, {13, 96}, {12, 80}, {11, 56}, {9, 40}, {8, 24}};
static const CbPair cb_E_11[] = {{16, 108}, {14, 96}, {13, 84}, {12, 80}, {11, 48}};
static const CbPair cb_E_12[] = {{16, 16}};
static const CbPair cb_E_13[] = {{16, 16}};
static const CbPair cb_E_14[] = {{11, 44}, {9, 32}};
static const CbPair cb_E_15[] = {{16, 16}};
static const CbPair cb_E_16[] = {{16, 16}};
static const CbPair cb_E_17[] = {{16, 16}};

static const CbPair cb_F_2[] = {{13, 256}, {12, 224}, {11, 128}, {10, 96}, {9, 80}, {8, 60}};
static const CbPair cb_F_3[] = {{14, 256}, {13, 192}, {11, 160}, {11, 80}, {8, 48}};
static const CbPair cb_F_4[] = {{11, 192}, {10, 128}, {9, 64}, {8, 48}};
static const CbPair cb_F_5[] = {{11, 32}};
static const CbPair cb_F_6[] = {{12, 192}, {11, 160}, {10, 128}, {13, 64}, {10, 32}};
static const CbPair cb_F_7[] = {{14, 384}, {13, 288}, {11, 240}, {10, 120}, {8, 48}};
static const CbPair cb_F_8[] = {{14, 192}, {11, 160}, {10, 128}, {9, 96}};
static const CbPair cb_F_9[] = {{14, 192}, {13, 176}, {11, 160}, {10, 96}};
static const CbPair cb_F_10[] = {{14, 256}, {13, 192}, {11, 160}, {10, 80}, {8, 48}};
static const CbPair cb_F_11[] = {{16, 192}, {14, 160}, {13, 128}, {12, 112}, {11, 80}, {10, 44}};
static const CbPair cb_F_12[] = {{16, 16}};
static const CbPair cb_F_13[] = {{16, 16}};
static const CbPair cb_F_14[] = {{11, 44}, {9, 32}};
static const CbPair cb_F_15[] = {{16, 16}};
static const CbPair cb_F_16[] = {{16, 16}};
static const CbPair cb_F_17[] = {{16, 16}};

const CbEnt root_A[18] = {
    {0, 0},
    {cb_A_1, 5},
    {cb_A_2, 6},
    {cb_A_3, 6},
    {cb_A_4, 1},
    {cb_A_5, 1},
    {cb_A_6, 5},
    {cb_A_7, 6},
    {cb_A_8, 5},
    {cb_A_9, 5},
    {cb_A_10, 6},
    {cb_A_11, 5},
    {cb_A_12, 1},
    {cb_A_13, 1},
    {cb_A_14, 2},
    {cb_A_15, 1},
    {cb_A_16, 1},
    {cb_A_17, 1}
};

const CbEnt root_B[18] = {
    {cb_B_0, 2},
    {cb_B_1, 6},
    {cb_B_2, 6},
    {cb_B_3, 5},
    {cb_B_4, 4},
    {cb_B_5, 1},
    {cb_B_6, 5},
    {cb_B_7, 5},
    {cb_B_8, 4},
    {cb_B_9, 4},
    {cb_B_10, 5},
    {cb_B_11, 6},
    {cb_B_12, 1},
    {cb_B_13, 1},
    {cb_B_14, 2},
    {cb_B_15, 1},
    {cb_B_16, 1},
    {cb_B_17, 1}
};

const CbEnt root_C[18] = {
    {0, 0},
    {cb_C_1, 5},
    {cb_C_2, 6},
    {cb_C_3, 6},
    {cb_C_4, 1},
    {cb_C_5, 1},
    {cb_C_6, 5},
    {cb_C_7, 6},
    {cb_C_8, 5},
    {cb_C_9, 5},
    {cb_C_10, 6},
    {cb_C_11, 5},
    {cb_C_12, 1},
    {cb_C_13, 1},
    {cb_C_14, 2},
    {cb_C_15, 1},
    {cb_C_16, 1},
    {cb_C_17, 1}
};

const CbEnt root_D[18] = {
    {0, 0},
    {cb_D_1, 4},
    {cb_D_2, 6},
    {cb_D_3, 5},
    {cb_D_4, 4},
    {cb_D_5, 1},
    {cb_D_6, 5},
    {cb_D_7, 5},
    {cb_D_8, 4},
    {cb_D_9, 4},
    {cb_D_10, 5},
    {cb_D_11, 6},
    {cb_D_12, 1},
    {cb_D_13, 1},
    {cb_D_14, 2},
    {cb_D_15, 1},
    {cb_D_16, 1},
    {cb_D_17, 1}
};

const CbEnt root_E[18] = {
    {0, 0},
    {0, 0},
    {cb_E_2, 6},
    {cb_E_3, 6},
    {cb_E_4, 1},
    {cb_E_5, 1},
    {cb_E_6, 5},
    {cb_E_7, 6},
    {cb_E_8, 5},
    {cb_E_9, 5},
    {cb_E_10, 6},
    {cb_E_11, 5},
    {cb_E_12, 1},
    {cb_E_13, 1},
    {cb_E_14, 2},
    {cb_E_15, 1},
    {cb_E_16, 1},
    {cb_E_17, 1}
};

const CbEnt root_F[18] = {
    {0, 0},
    {0, 0},
    {cb_F_2, 6},
    {cb_F_3, 5},
    {cb_F_4, 4},
    {cb_F_5, 1},
    {cb_F_6, 5},
    {cb_F_7, 5},
    {cb_F_8, 4},
    {cb_F_9, 4},
    {cb_F_10, 5},
    {cb_F_11, 6},
    {cb_F_12, 1},
    {cb_F_13, 1},
    {cb_F_14, 2},
    {cb_F_15, 1},
    {cb_F_16, 1},
    {cb_F_17, 1}
};
