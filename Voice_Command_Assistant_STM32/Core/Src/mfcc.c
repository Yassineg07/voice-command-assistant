#include "mfcc.h"
#include <math.h>

#ifndef ARM_MATH_CM4
#define ARM_MATH_CM4
#endif
#include "arm_math.h"

#ifndef PI
#define PI 3.14159265358979323846f
#endif

// 1. Core state arrays
static float window[MFCC_FRAME_LEN];
static float mel_weights[MFCC_NUM_BANDS][MFCC_FFT_LEN / 2];
static arm_rfft_fast_instance_f32 rfft_inst;

// Math Helper: Convert Hz to human-perceptual Mel scale
static float hz_to_mel(float hz) {
  return 2595.0f * log10f(1.0f + hz / 700.0f);
}

// Math Helper: Convert Mel scale back to Hz
static float mel_to_hz(float mel) {
  return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

void MFCC_Init(void) {
  // 1. Initialize ARM's blazing fast hardware-optimized FFT
  arm_rfft_fast_init_f32(&rfft_inst, MFCC_FFT_LEN);

  // 2. Precompute the Hanning Window curve
  for (int i = 0; i < MFCC_FRAME_LEN; i++) {
    window[i] = 0.5f - 0.5f * cosf(2.0f * PI * (float)i / (MFCC_FRAME_LEN - 1));
  }

  // 3. Dynamically construct the Mel Filterbank matrix!
  // This avoids having to paste 1000 lines of weird constants.
  // It runs exactly once at boot and takes ~5KB of RAM.
  float sample_rate = 8000.0f;
  int num_bins = MFCC_FFT_LEN / 2;
  float mel_min = hz_to_mel(20.0f);   // Human hearing floor
  float mel_max = hz_to_mel(4000.0f); // Hardware ceiling (Nyquist)

  float mel_points[MFCC_NUM_BANDS + 2];
  for (int i = 0; i < MFCC_NUM_BANDS + 2; i++) {
    mel_points[i] = mel_min + i * (mel_max - mel_min) / (MFCC_NUM_BANDS + 1);
  }

  int bin_points[MFCC_NUM_BANDS + 2];
  for (int i = 0; i < MFCC_NUM_BANDS + 2; i++) {
    float hz = mel_to_hz(mel_points[i]);
    bin_points[i] = (int)((hz / (sample_rate / 2.0f)) * num_bins);
  }

  for (int b = 0; b < MFCC_NUM_BANDS; b++) {
    int start = bin_points[b];
    int center = bin_points[b + 1];
    int end = bin_points[b + 2];

    for (int i = 0; i < num_bins; i++) {
      if (i >= start && i <= center) {
        mel_weights[b][i] = (float)(i - start) / (center - start);
      } else if (i > center && i <= end) {
        mel_weights[b][i] = (float)(end - i) / (end - center);
      } else {
        mel_weights[b][i] = 0.0f;
      }
    }
  }
}

void MFCC_Compute(const int16_t *audio_in, float *mfcc_out) {
  // Moved to static BSS to avoid instantly blowing up the 2048-byte RTOS Stack!
  static float fft_in[MFCC_FFT_LEN];
  static float fft_out[MFCC_FFT_LEN];
  static float mag[MFCC_FFT_LEN / 2];

  // 0. Calculate DC offset (mean subtraction) to eliminate hardware microphone
  // bias
  float mean = 0.0f;
  for (int i = 0; i < MFCC_FRAME_LEN; i++) {
    mean += audio_in[i];
  }
  mean /= (float)MFCC_FRAME_LEN;

  // 1a. Convert 16-bit PCM integer to float, remove DC offset, and apply the
  // Hanning curve
  for (int i = 0; i < MFCC_FRAME_LEN; i++) {
    fft_in[i] = ((float)audio_in[i] - mean) * window[i];
  }

  // 1b. Manually zero-pad the rest of the array since it's statically allocated
  for (int i = MFCC_FRAME_LEN; i < MFCC_FFT_LEN; i++) {
    fft_in[i] = 0.0f;
  }

  // 2. Perform the Fast Fourier Transform (Time -> Frequency domain)
  arm_rfft_fast_f32(&rfft_inst, fft_in, fft_out, 0);

  // 3. Convert complex imaginary outputs into physical Magnitude
  arm_cmplx_mag_f32(fft_out, mag, MFCC_FFT_LEN / 2);

  // 4. Dot Product: Matrix Multiply the FFT against the Mel Filterbank
  for (int b = 0; b < MFCC_NUM_BANDS; b++) {
    float energy = 0.0f;
    for (int i = 0; i < MFCC_FFT_LEN / 2; i++) {
      energy += mag[i] * mel_weights[b][i];
    }

    // 5. Apply the Logarithm to squish the dynamic range
    // Python's librosa.power_to_db literally translates to: 10 * log10(x / ref)
    mfcc_out[b] = 10.0f * log10f(energy + 1e-8f);
  }
}
