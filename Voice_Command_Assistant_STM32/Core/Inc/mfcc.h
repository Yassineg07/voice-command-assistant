#ifndef MFCC_H
#define MFCC_H

#include <stdint.h>

#define MFCC_FRAME_LEN 512
#define MFCC_FFT_LEN 512
#define MFCC_NUM_BANDS 10

void MFCC_Init(void);
void MFCC_Compute(const int16_t *audio_in, float *mfcc_out);

#endif /* MFCC_H */
