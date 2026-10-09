// Optional native Blip25 decoder. Each P25 decoder owns its own channel state.
#ifndef INCLUDED_BLIP25_DECODER_H
#define INCLUDED_BLIP25_DECODER_H

#include <cstddef>
#include <cstdint>

class blip25_decoder {
public:
    explicit blip25_decoder(unsigned phase) : d_phase(phase) {}
    ~blip25_decoder();
    blip25_decoder(const blip25_decoder&) = delete;
    blip25_decoder& operator=(const blip25_decoder&) = delete;
    void select(bool enabled);
    bool enabled() const { return d_enabled; }
    void decode(const uint16_t *info, size_t count, uint32_t errors,
                bool lost, bool mute, int16_t *samples);

private:
    unsigned d_phase;
    bool d_enabled = false;
    void *d_handle = nullptr;
};
#endif
