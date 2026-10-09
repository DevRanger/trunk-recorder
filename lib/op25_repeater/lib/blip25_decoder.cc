#include "blip25_decoder.h"
#include <algorithm>
#include <stdexcept>

#ifdef TR_HAVE_BLIP25
extern "C" {
    void *tr_blip25_create(uint32_t phase);
    int32_t tr_blip25_reset(void *handle);
    int32_t tr_blip25_decode(void *handle, const uint16_t *info, size_t count,
                           uint32_t errors, int32_t lost, int32_t mute,
                           int16_t *output, size_t output_count);
    void tr_blip25_destroy(void *handle);
}
#endif

blip25_decoder::~blip25_decoder() {
#ifdef TR_HAVE_BLIP25
    tr_blip25_destroy(d_handle);
#endif
}

void blip25_decoder::select(bool enabled) {
    d_enabled = enabled;
    if (!enabled) return;
#ifdef TR_HAVE_BLIP25
    if (!d_handle) d_handle = tr_blip25_create(d_phase);
    if (!d_handle || tr_blip25_reset(d_handle) != 0)
        throw std::runtime_error("Unable to initialize Blip25 decoder");
#else
    throw std::runtime_error("Blip25 support was not enabled at build time");
#endif
}

void blip25_decoder::decode(const uint16_t *info, size_t count, uint32_t errors,
                           bool lost, bool mute, int16_t *samples) {
    std::fill_n(samples, 160, 0);
#ifdef TR_HAVE_BLIP25
    if (!d_enabled || !d_handle) return;
    const int result = tr_blip25_decode(d_handle, info, count, errors, lost, mute, samples, 160);
    // Never expose partial output on failure or speech from an encrypted frame.
    if (result != 0 || mute) std::fill_n(samples, 160, 0);
#endif
}
