/*
MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifdef FEATURE_A2_AUDIO

#include "audio_ring.h"
#include <hardware/sync.h>

void __time_critical_func(audio_ring_set)(audio_ring_t *audio_ring, audio_sample_t *buffer, uint32_t size) {
    assert(size > 1);
    audio_ring->buffer = buffer;
    audio_ring->size   = size;
    audio_ring->read   = 0;
    audio_ring->write  = 0;
}

uint32_t __time_critical_func(get_write_size)(audio_ring_t *audio_ring, bool full) {
    __mem_fence_acquire();
    uint32_t rp = audio_ring->read;
    uint32_t wp = audio_ring->write;
    if (wp < rp) {
        return rp - wp - 1;
    } else {
        return audio_ring->size - wp + (full ? rp - 1 : (rp == 0 ? -1 : 0));
    }   
}

uint32_t __time_critical_func(get_read_size)(audio_ring_t *audio_ring, bool full) {
    __mem_fence_acquire();
    uint32_t rp = audio_ring->read;
    uint32_t wp = audio_ring->write;
    
    if (wp < rp) {
        return audio_ring->size - rp + (full ? wp : 0);
    } else {
        return wp - rp;
    }    
}

void __time_critical_func(increase_write_pointer)(audio_ring_t *audio_ring, uint32_t size) {
    audio_ring->write = (audio_ring->write + size) & (audio_ring->size - 1);
    __mem_fence_release();
}

void __time_critical_func(increase_read_pointer)(audio_ring_t *audio_ring, uint32_t size) {
    audio_ring->read = (audio_ring->read + size) & (audio_ring->size - 1);
    __mem_fence_release();
}

void __time_critical_func(set_write_offset)(audio_ring_t *audio_ring, uint32_t v) {
    audio_ring->write = v;
    __mem_fence_release();
}

void __time_critical_func(set_read_offset)(audio_ring_t *audio_ring, uint32_t v) {
    audio_ring->read = v;
    __mem_fence_release();
}

#endif  //   FEATURE_A2_AUDIO