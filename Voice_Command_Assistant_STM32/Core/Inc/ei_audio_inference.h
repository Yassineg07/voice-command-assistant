#ifndef EI_AUDIO_INFERENCE_H
#define EI_AUDIO_INFERENCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>


/**
 * @brief Initialise the Edge Impulse inference engine.
 *        Call once before starting the FreeRTOS audio task.
 */
void ei_inference_init(void);

/**
 * @brief Feed raw mono PCM int16 samples (at 8 kHz) into the EI SDK.
 *        Call every time the PDM decoder produces new samples.
 *
 * @param buf  Pointer to int16 PCM sample array
 * @param len  Number of samples in buf
 */
void ei_inference_feed(const int16_t *buf, uint32_t len);

/**
 * @brief Poll for a fresh inference result.
 *
 * @param out_scores  Output array of 3 floats:
 *                      [0] = Noise probability
 *                      [1] = Solayl probability  (wake word)
 *                      [2] = Turn on probability (action word)
 * @return true  if out_scores contains a brand-new result
 * @return false if no new inference has completed since last call
 */
bool ei_inference_get_scores(float out_scores[3]);

#ifdef __cplusplus
}
#endif

#endif /* EI_AUDIO_INFERENCE_H */
