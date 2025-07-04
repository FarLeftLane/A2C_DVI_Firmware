/*
MIT License

Copyright (c) 2024 Thorsten Brehm

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

#include "hardware/clocks.h"

#include "a2dvi.h"
#include "dvi.h"
#include "tmds.h"
#include "dvi_pin_config.h"
#include "dvi_serialiser.h"
#include "dvi_timing.h"
#include "render/render.h"
#include "util/dmacopy.h"
#include "config/config.h"
#include "debug/debug.h"
#ifdef FEATURE_A2C
#include "a2c/a2c.h"
#endif

#define DVI_SERIAL_CONFIG pico_a2dvi_cfg

// struct dvi_inst __attribute__((section (".appledata."))) dvi0;
struct dvi_inst dvi0;           //  Need to move this to normal RAM or there is an init race condition 

static void a2dvi_init(void)
{
    // wait a bit, until the raised core VCC has settled
    sleep_ms(2);
    // shift into higher gears...
    set_sys_clock_khz(dvi_timing_640x480p_60hz.bit_clk_khz, true);
}

void DELAYED_COPY_CODE(a2dvi_dvi_enable)(uint32_t video_mode)
{
    static uint32_t current_video_mode = DviInvalid;
    static uint     spinlock1;
    static uint     spinlock2;

    if (current_video_mode == DviInvalid)
    {
        spinlock1 = next_striped_spin_lock_num();
        spinlock2 = next_striped_spin_lock_num();
    }
    else
    {
        if (current_video_mode == video_mode)
            return;
        dvi_destroy(&dvi0, DMA_IRQ_0);
    }

    // remember current mode
    current_video_mode = video_mode;

    // select timing
    struct dvi_timing* p_dvi_timing = (video_mode == Dvi720x480) ? &dvi_timing_720x480p_60hz : &dvi_timing_640x480p_60hz;

    // configure DVI
    set_sys_clock_khz(p_dvi_timing->bit_clk_khz, true);
    DVI_INIT_RESOLUTION(p_dvi_timing->h_active_pixels);
    dvi0.timing = p_dvi_timing;
    dvi0.ser_cfg = &DVI_SERIAL_CONFIG;
    dvi_init(&dvi0, spinlock1, spinlock2);

    // Audio Init
#ifdef FEATURE_A2_AUDIO
    switch (video_mode)
    {
        case Dvi640x480:
            dvi_set_audio_freq(&dvi0, 44100, 28000, 6272);          //  640x480 = 25.2MHz = 28000/6272
            break;

        case Dvi720x480:
            dvi_set_audio_freq(&dvi0, 44100, 30000, 6272);          //  720x480 = 27MHz = 30000/6272 
            break;
        
        default:
            cfg_audio_enabled = false;                              //  We don't knwo the right rates
            break;
    }

    dvi_enable_data_island(&dvi0);

    a2dvi_audio_enable(cfg_audio_enabled);
#endif

    dvi_register_irqs_this_core(&dvi0, DMA_IRQ_0);
    dvi_start(&dvi0);
}

void DELAYED_COPY_CODE(a2dvi_loop)(void)
{
    // CPU clock configuration required for DVI
    a2dvi_init();

    // load TMDS color palette from flash (with DMA)
    tmds_color_load();

    // free DMA channel and stop others from using it (would interfere with the DVI processing)
    dmacopy_disable_dma();
    // when testing: release flash, so we can access the BOOTSEL button
    debug_flash_release();

    // load character sets etc
    render_init();

    // start DVI output
    render_loop();

    __builtin_unreachable();
}

uint32_t DELAYED_COPY_CODE(a2dvi_scanline_errors)(void)
{
    return dvi0.scanline_errors;
}

bool DELAYED_COPY_CODE(a2dvi_started)(void)
{
    return dvi_is_started(&dvi0);
}

#ifdef FEATURE_A2_AUDIO

void DELAYED_COPY_CODE(a2dvi_audio_enable)(bool enable)
{
    cfg_audio_enabled = enable;
    dvi_audio_enable(&dvi0, enable);
#ifdef FEATURE_A2C
    a2c_audio_enable(enable);
#endif
}

bool DELAYED_COPY_CODE(a2dvi_audio_enabled)(void)
{
    return dvi0.audio_enabled;
}

bool DELAYED_COPY_CODE(a2dvi_queue_audio_samples)(const int16_t* samples, int count)
{
    return dvi_queue_audio_samples(&dvi0, samples, count);
}

#endif  // FEATURE_A2_AUDIO