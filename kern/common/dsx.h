#pragma once
#include <stdint.h>

void dsee_f76_frame(uint8_t *mem, float *pcm, int a3, int a4, int a5);
void dsee_f80_pipeline(uint8_t *mem, float *pcm, int a3, int a4);
void dsee_dsp(uint8_t *mem, float *pcm, int a3, int a4);

int dsee_pick_level(int channels, int vbr, int content_hz, int src_hz,
                    int codec, unsigned energy, int bit_override,
                    int *level);
