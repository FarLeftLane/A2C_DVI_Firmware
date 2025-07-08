#include <stdlib.h>
#include "hardware/dma.h"
#include "hardware/irq.h"

#include "dvi.h"
#include "dvi_timing.h"
#include "dvi_serialiser.h"
#include "tmds_encode.h"

// just hang when something isn't right
#define panic(x) {while (1);}

#if 0
// Time-critical functions pulled into RAM but each in a unique section to
// allow garbage collection
#define __dvi_func(f) __not_in_flash_func(f)
#define __dvi_func_x(f) __scratch_x(__STRING(f)) f
#endif

// We require exclusive use of a DMA IRQ line. (you wouldn't want to share
// anyway). It's possible in theory to hook both IRQs and have two DVI outs.
static struct dvi_inst *dma_irq_privdata[2];
static void dvi_dma0_irq();
static void dvi_dma1_irq();

#define A2DVI_SCANLINES (2*192 + 4*16)

void __dvi_func(dvi_init)(struct dvi_inst *inst, uint spinlock_tmds_queue, uint spinlock_colour_queue)
{
	inst->dvi_started = false;
    inst->timing_state.v_ctr  = 0;
	inst->data_island_is_enabled = false;
	inst->audio_enabled = false;
#ifdef FEATURE_A2_AUDIO
    inst->dvi_frame_count = 0;
    dvi_audio_init(inst);
#endif

	dvi_timing_state_init(&inst->timing_state);
	dvi_serialiser_init(inst->ser_cfg);
	for (int i = 0; i < N_TMDS_LANES; ++i) {
		inst->dma_cfg[i].chan_ctrl = dma_claim_unused_channel(true);
		inst->dma_cfg[i].chan_data = dma_claim_unused_channel(true);
		inst->dma_cfg[i].tx_fifo = (void*)&inst->ser_cfg->pio->txf[inst->ser_cfg->sm_tmds[i]];
		inst->dma_cfg[i].dreq = pio_get_dreq(inst->ser_cfg->pio, inst->ser_cfg->sm_tmds[i], true);
	}
	inst->late_scanline_ctr = 0;
	inst->scanline_emulation = 0;
	inst->scanline_errors = 0;
	inst->tmds_buf_release_next = NULL;
	inst->tmds_buf_release = NULL;
	queue_init_with_spinlock(&inst->q_tmds_valid,   sizeof(void*),  8, spinlock_tmds_queue);
	queue_init_with_spinlock(&inst->q_tmds_free,    sizeof(void*),  8, spinlock_tmds_queue);
#if 0
	queue_init_with_spinlock(&inst->q_colour_valid, sizeof(void*),  8, spinlock_colour_queue);
	queue_init_with_spinlock(&inst->q_colour_free,  sizeof(void*),  8, spinlock_colour_queue);
#endif

	dvi_setup_scanline_for_vblank(inst->timing, inst->dma_cfg, true,  &inst->dma_list_vblank_sync);
	dvi_setup_scanline_for_vblank(inst->timing, inst->dma_cfg, false, &inst->dma_list_vblank_nosync);
	dvi_setup_scanline_for_active(inst->timing, inst->dma_cfg, (void*)SRAM_BASE, &inst->dma_list_active, false);
	dvi_setup_scanline_for_active(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_error, false);
#ifdef FEATURE_A2_AUDIO
    dvi_setup_scanline_for_active(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_active_blank, true);
#endif

	for (int i = 0; i < DVI_N_TMDS_BUFFERS; ++i)
	{
		void *tmdsbuf;
#if DVI_MONOCHROME_TMDS
		tmdsbuf = malloc(inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD * sizeof(uint32_t));
#else
		tmdsbuf = malloc(3 * inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD * sizeof(uint32_t));

		if (tmdsbuf)
		{
			// initialize all TMDS buffers with black pixels
			for (int j=0;j<3 * inst->timing->h_active_pixels / 2;j++)
				((uint32_t*)tmdsbuf)[j] = 0x7fd00;
		}
#endif
		if (!tmdsbuf)
			panic("TMDS buffer allocation failed");
		queue_add_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
	}

#ifdef FEATURE_A2_AUDIO
    set_AVI_info_frame(&inst->avi_info_frame, UNDERSCAN, RGB, ITU601, PIC_ASPECT_RATIO_4_3, SAME_AS_PAR, FULL, _640x480P60);	
#endif
}

// The IRQs will run on whichever core calls this function (this is why it's
// called separately from dvi_init)
void __dvi_func(dvi_register_irqs_this_core)(struct dvi_inst *inst, uint irq_num)
{
	uint32_t mask_sync_channel = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;
	uint32_t mask_all_channels = 0;
	for (int i = 0; i < N_TMDS_LANES; ++i)
		mask_all_channels |= 1u << inst->dma_cfg[i].chan_ctrl | 1u << inst->dma_cfg[i].chan_data;

	dma_hw->ints0 = mask_sync_channel;
	if (irq_num == DMA_IRQ_0) {
		hw_write_masked(&dma_hw->inte0, mask_sync_channel, mask_all_channels);
		dma_irq_privdata[0] = inst;
		irq_set_exclusive_handler(DMA_IRQ_0, dvi_dma0_irq);
	}
	else {
		hw_write_masked(&dma_hw->inte1, mask_sync_channel, mask_all_channels);
		dma_irq_privdata[1] = inst;
		irq_set_exclusive_handler(DMA_IRQ_1, dvi_dma1_irq);
	}
	irq_set_enabled(irq_num, true);
}

// The IRQs will run on whichever core calls this function (this is why it's
// called separately from dvi_init)
void __dvi_func(dvi_unregister_irqs)(struct dvi_inst *inst, uint irq_num)
{
	irq_set_enabled(irq_num, false);

	uint32_t mask_all_channels = 0;
	for (int i = 0; i < N_TMDS_LANES; ++i)
		mask_all_channels |= 1u << inst->dma_cfg[i].chan_ctrl | 1u << inst->dma_cfg[i].chan_data;

	dma_hw->ints0 = 0;

	if (irq_num == DMA_IRQ_0) {
		hw_write_masked(&dma_hw->inte0, 0, mask_all_channels);
		dma_irq_privdata[0] = 0;
		//irq_set_exclusive_handler(DMA_IRQ_0, 0);
	}
	else {
		hw_write_masked(&dma_hw->inte1, 0, mask_all_channels);
		dma_irq_privdata[1] = 0;
		//irq_set_exclusive_handler(DMA_IRQ_1, 0);
	}
}

// Set up control channels to make transfers to data channels' control
// registers (but don't trigger the control channels -- this is done either by
// data channel CHAIN_TO or an initial write to MULTI_CHAN_TRIGGER)
static inline void __attribute__((always_inline)) _dvi_load_dma_op(const struct dvi_lane_dma_cfg dma_cfg[], struct dvi_scanline_dma_list *l) {
	for (int i = 0; i < N_TMDS_LANES; ++i) {
		dma_channel_config cfg = dma_channel_get_default_config(dma_cfg[i].chan_ctrl);
		channel_config_set_ring(&cfg, true, 4); // 16-byte write wrap
		channel_config_set_read_increment(&cfg, true);
		channel_config_set_write_increment(&cfg, true);
		dma_channel_configure(
			dma_cfg[i].chan_ctrl,
			&cfg,
			&dma_hw->ch[dma_cfg[i].chan_data],
			dvi_lane_from_list(l, i),
			4, // Configure all 4 registers then halt until next CHAIN_TO
			false
		);
	}
}

// Setup first set of control block lists, configure the control channels, and
// trigger them. Control channels will subsequently be triggered only by DMA
// CHAIN_TO on data channel completion. IRQ handler *must* be prepared before
// calling this. (Hooked to DMA IRQ0)
void __dvi_func(dvi_start)(struct dvi_inst *inst) {
	if (inst->dvi_started) {
        return;
    }
    
	_dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_vblank_nosync);
	dma_start_channel_mask(
		(1u << inst->dma_cfg[0].chan_ctrl) |
		(1u << inst->dma_cfg[1].chan_ctrl) |
		(1u << inst->dma_cfg[2].chan_ctrl));

	// We really don't want the FIFOs to bottom out, so wait for full before
	// starting the shift-out.
	for (int i = 0; i < N_TMDS_LANES; ++i)
		while (!pio_sm_is_tx_fifo_full(inst->ser_cfg->pio, inst->ser_cfg->sm_tmds[i]))
			tight_loop_contents();
	dvi_serialiser_enable(inst->ser_cfg, true);
	
	inst->dvi_started = true;
}

void dvi_stop(struct dvi_inst *inst) {
    if (!inst->dvi_started) {
        return;
    }
    uint mask  = 0;
    for (int i = 0; i < N_TMDS_LANES; ++i) {
        dma_channel_config cfg = dma_channel_get_default_config(inst->dma_cfg[i].chan_ctrl);
        dma_channel_set_config(inst->dma_cfg[i].chan_ctrl, &cfg, false);
        cfg = dma_channel_get_default_config(inst->dma_cfg[i].chan_data);
        dma_channel_set_config(inst->dma_cfg[i].chan_data, &cfg, false);
        mask |= 1 << inst->dma_cfg[i].chan_data;
        mask |= 1 << inst->dma_cfg[i].chan_ctrl;
    }

    dma_channel_abort(mask);
    dma_irqn_acknowledge_channel(0, inst->dma_cfg[TMDS_SYNC_LANE].chan_data);
    dma_hw->ints0 = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;

    dvi_serialiser_enable(inst->ser_cfg, false);
    inst->dvi_started = false;
}

#if 0 // DISABLED: not used by A2DVI
static inline void __dvi_func_x(_dvi_prepare_scanline_8bpp)(struct dvi_inst *inst, uint32_t *scanbuf) {
	uint32_t *tmdsbuf = NULL;
	queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
	uint pixwidth = inst->timing->h_active_pixels;
	uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
	// Scanline buffers are half-resolution; the functions take the number of *input* pixels as parameter.
	tmds_encode_data_channel_8bpp(scanbuf, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_8BPP_BLUE_MSB,  DVI_8BPP_BLUE_LSB );
	tmds_encode_data_channel_8bpp(scanbuf, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_8BPP_GREEN_MSB, DVI_8BPP_GREEN_LSB);
	tmds_encode_data_channel_8bpp(scanbuf, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_8BPP_RED_MSB,   DVI_8BPP_RED_LSB  );
	queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);
}

static inline void __dvi_func_x(_dvi_prepare_scanline_16bpp)(struct dvi_inst *inst, uint32_t *scanbuf) {
	uint32_t *tmdsbuf = NULL;
	queue_remove_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
	uint pixwidth = inst->timing->h_active_pixels;
	uint words_per_channel = pixwidth / DVI_SYMBOLS_PER_WORD;
	tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 0 * words_per_channel, pixwidth / 2, DVI_16BPP_BLUE_MSB,  DVI_16BPP_BLUE_LSB );
	tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 1 * words_per_channel, pixwidth / 2, DVI_16BPP_GREEN_MSB, DVI_16BPP_GREEN_LSB);
	tmds_encode_data_channel_16bpp(scanbuf, tmdsbuf + 2 * words_per_channel, pixwidth / 2, DVI_16BPP_RED_MSB,   DVI_16BPP_RED_LSB  );
	queue_add_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);
}

// "Worker threads" for TMDS encoding (core enters and never returns, but still handles IRQs)

// Version where each record in q_colour_valid is one scanline:
void __dvi_func(dvi_scanbuf_main_8bpp)(struct dvi_inst *inst) {
	uint y = 0;
	while (1) {
		uint32_t *scanbuf = NULL;
		queue_remove_blocking_u32(&inst->q_colour_valid, &scanbuf);
		_dvi_prepare_scanline_8bpp(inst, scanbuf);
		queue_add_blocking_u32(&inst->q_colour_free, &scanbuf);
		++y;
		if (y == inst->timing->v_active_lines) {
			y = 0;
		}
	}
	__builtin_unreachable();
}

// Ugh copy/paste but it lets us garbage collect the TMDS stuff that is not being used from .scratch_x
void __dvi_func(dvi_scanbuf_main_16bpp)(struct dvi_inst *inst) {
	uint y = 0;
	while (1) {
		uint32_t *scanbuf;
		queue_remove_blocking_u32(&inst->q_colour_valid, &scanbuf);
		_dvi_prepare_scanline_16bpp(inst, scanbuf);
		queue_add_blocking_u32(&inst->q_colour_free, &scanbuf);
		++y;
		if (y == inst->timing->v_active_lines) {
			y = 0;
		}
	}
	__builtin_unreachable();
}
#endif // DISABLED: not used by A2DVI

static void __dvi_func(dvi_dma_irq_handler)(struct dvi_inst *inst)
{
	// Every fourth interrupt marks the start of the horizontal active region. We
	// now have until the end of this region to generate DMA blocklist for next
	// scanline.
	dvi_timing_state_advance(inst->timing, &inst->timing_state);
	if (inst->tmds_buf_release && !queue_try_add_u32(&inst->q_tmds_free, &inst->tmds_buf_release))
		panic("TMDS free queue full in IRQ!");
	inst->tmds_buf_release = inst->tmds_buf_release_next;
	inst->tmds_buf_release_next = NULL;

	// Make sure all three channels have definitely loaded their last block
	// (should be within a few cycles of one another)
	for (int i = 0; i < N_TMDS_LANES; ++i) {
		while (dma_debug_hw->ch[inst->dma_cfg[i].chan_data].dbg_tcr != inst->timing->h_active_pixels / DVI_SYMBOLS_PER_WORD)
			tight_loop_contents();							//	Have not seen this condition hit  640 or 720
	}

	uint32_t *tmdsbuf;
	while ((inst->late_scanline_ctr > 0) && (queue_try_remove_u32(&inst->q_tmds_valid, &tmdsbuf)))
	{
		// If we displayed this buffer then it would be in the wrong vertical
		// position on-screen. Just pass it back.
		queue_add_blocking_u32(&inst->q_tmds_free, &tmdsbuf);
		--inst->late_scanline_ctr;
	}

	// blank lines (overscan area, first 48 lines, last 48 lines (apple II letter box), and scanlines)
	if ((inst->timing_state.v_state != DVI_STATE_ACTIVE)||
		(inst->timing_state.v_ctr < (inst->timing->v_active_lines-A2DVI_SCANLINES)/2)||
		(((inst->scanline_emulation)&&(inst->timing_state.v_ctr & 1)==0))||
		(inst->timing_state.v_ctr >= (inst->timing->v_active_lines/2+A2DVI_SCANLINES/2)))
	{
		// Don't care
		tmdsbuf = NULL;
	}
	else
	if (queue_try_peek_u32(&inst->q_tmds_valid, &tmdsbuf))
	{
		if (inst->timing_state.v_ctr % DVI_VERTICAL_REPEAT == DVI_VERTICAL_REPEAT - 1) {
			queue_remove_blocking_u32(&inst->q_tmds_valid, &tmdsbuf);
			inst->tmds_buf_release_next = tmdsbuf;
		}
	}
	else {
		// No valid scanline was ready
		tmdsbuf = NULL;
		++inst->scanline_errors;
		if (inst->timing_state.v_ctr % DVI_VERTICAL_REPEAT == DVI_VERTICAL_REPEAT - 1)
			++inst->late_scanline_ctr;
	}

	switch (inst->timing_state.v_state) {
		case DVI_STATE_ACTIVE:
			if (tmdsbuf) {
				dvi_update_scanline_data_dma(inst->timing, tmdsbuf, &inst->dma_list_active, inst->data_island_is_enabled);
				_dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_active);
			}
			else {
				_dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_error);
			}
#if 0
			if (inst->scanline_callback && inst->timing_state.v_ctr % DVI_VERTICAL_REPEAT == DVI_VERTICAL_REPEAT - 1) {
				inst->scanline_callback();
			}
#endif
			break;
		case DVI_STATE_SYNC:
			_dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_vblank_sync);
#ifdef FEATURE_A2_AUDIO
            if (inst->timing_state.v_ctr == 0) {
                ++inst->dvi_frame_count;
            }
#endif
			break;
		//case DVI_STATE_FRONT_PORCH:
		//case DVI_STATE_BACK_PORCH:
		default:
			_dvi_load_dma_op(inst->dma_cfg, &inst->dma_list_vblank_nosync);
			break;
	}
#ifdef FEATURE_A2_AUDIO
    if (inst->data_island_is_enabled) {
		if (inst->audio_enabled)
        	dvi_update_data_stream(inst);
		else
			dvi_update_data_stream_null(inst);
    }
#endif
}

static void __dvi_func(dvi_dma0_irq)() {
	struct dvi_inst *inst = dma_irq_privdata[0];
	dma_hw->ints0 = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;
	dvi_dma_irq_handler(inst);
}

static void __dvi_func(dvi_dma1_irq)() {
	struct dvi_inst *inst = dma_irq_privdata[1];
	dma_hw->ints1 = 1u << inst->dma_cfg[TMDS_SYNC_LANE].chan_data;
	dvi_dma_irq_handler(inst);
}

// must be called on the CPU core which is running the IRQ handlers
void __dvi_func(dvi_destroy)(struct dvi_inst *inst, uint irq_num)
{
	// disable IRQ
	dvi_unregister_irqs(inst, irq_num);

	// disable serialisers
	dvi_serialiser_enable(inst->ser_cfg, false);

	// clear serialiser PIO
	pio_clear_instruction_memory(inst->ser_cfg->pio);

	// remove tmds buffers from queues and free memory
	{
		uint buf_count = 0;
		if (inst->tmds_buf_release)
		{
			free(inst->tmds_buf_release);
			inst->tmds_buf_release = NULL;
			buf_count++;
		}
		if (inst->tmds_buf_release_next)
		{
			free(inst->tmds_buf_release_next);
			inst->tmds_buf_release_next = NULL;
			buf_count++;
		}
		while (buf_count < DVI_N_TMDS_BUFFERS)
		{
			void *tmdsbuf = NULL;
			// free queue
			if (queue_try_remove_u32(&inst->q_tmds_free, &tmdsbuf))
			{
				free(tmdsbuf);
				buf_count++;
			}
			// also consider valid queue, since we may have aborted a frame display cycle
			if (queue_try_remove_u32(&inst->q_tmds_valid, &tmdsbuf))
			{
				free(tmdsbuf);
				buf_count++;
			}
		}
	}

	// disable TMDS clock output
	for (uint i = inst->ser_cfg->pins_clk; i <= inst->ser_cfg->pins_clk + 1; ++i)
	{
		gpio_set_function(i, GPIO_FUNC_NULL);
	}

	// clean up DMA channels
	for (int i = 0; i < N_TMDS_LANES; ++i)
	{
		//pio_sm_drain_tx_fifo(inst->ser_cfg->pio, inst->ser_cfg->sm_tmds[i]);

		// clean up and free DMA channels
		dma_channel_cleanup(inst->dma_cfg[i].chan_ctrl);
		dma_channel_unclaim(inst->dma_cfg[i].chan_ctrl);

		dma_channel_cleanup(inst->dma_cfg[i].chan_data);
		dma_channel_unclaim(inst->dma_cfg[i].chan_data);

		// free serialiser PIO statemachines
		pio_sm_unclaim(inst->ser_cfg->pio, inst->ser_cfg->sm_tmds[i]);
	}

	// free queues
	queue_free(&inst->q_tmds_valid);
	queue_free(&inst->q_tmds_free);

#if 0
	queue_free(&inst->q_colour_valid);
	queue_free(&inst->q_colour_free);
#endif
}

#ifdef FEATURE_A2_AUDIO

#define NUMBER_OF_AUDIO_PACKETS 4
typedef struct data_island_streams {
    data_island_stream_t stream_true;
	data_island_stream_t stream_false;
} data_island_streams_t;

data_island_streams_t s_audio_data_streams[NUMBER_OF_AUDIO_PACKETS];	//	We calculate both the true and false versions of the stream on the render processor
data_island_stream_t s_zero_stream_true;								//	We cache the two most used null streams
data_island_stream_t s_zero_stream_false;

// DVI Data island related
void __dvi_func(dvi_audio_init)(struct dvi_inst *inst) {
    inst->data_island_is_enabled = false;
	inst->audio_enabled = false;
    inst->audio_freq = 0;
    inst->samples_per_frame = 0;
    inst->samples_per_line16 = 0;
    inst->audio_frame_count = 0;

	data_packet_t packet;
	set_null_data_packet(&packet);
    encode_data_packet(&s_zero_stream_true, &packet, true, inst->timing->h_sync_polarity);

	set_null_data_packet(&packet);
    encode_data_packet(&s_zero_stream_false, &packet, false, inst->timing->h_sync_polarity);

	//	We have a queue of audio data packets so that they can be processed on the other core
    uint spinlock3 = next_striped_spin_lock_num();
	queue_init_with_spinlock(&inst->q_audio_streams_free, sizeof(void*), NUMBER_OF_AUDIO_PACKETS, spinlock3);

	uint spinlock4 = next_striped_spin_lock_num();
	queue_init_with_spinlock(&inst->q_audio_streams_valid, sizeof(void*), NUMBER_OF_AUDIO_PACKETS, spinlock4);

	for (int i = 0; i < NUMBER_OF_AUDIO_PACKETS; i++)
	{
		encode_data_packet(&s_audio_data_streams[i].stream_true, &packet, true, inst->timing->h_sync_polarity);
		encode_data_packet(&s_audio_data_streams[i].stream_false, &packet, false, inst->timing->h_sync_polarity);
		void *stream = &s_audio_data_streams[i];
		queue_add_blocking_u32(&inst->q_audio_streams_free, &stream);
	}
}

void __dvi_func(dvi_enable_data_island)(struct dvi_inst *inst) {
    dvi_setup_scanline_for_vblank_with_audio(inst->timing, inst->dma_cfg, true, &inst->dma_list_vblank_sync);
    dvi_setup_scanline_for_vblank_with_audio(inst->timing, inst->dma_cfg, false, &inst->dma_list_vblank_nosync);
    dvi_setup_scanline_for_active_with_audio(inst->timing, inst->dma_cfg, (void*)SRAM_BASE, &inst->dma_list_active, false);
    dvi_setup_scanline_for_active_with_audio(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_error, false);
    dvi_setup_scanline_for_active_with_audio(inst->timing, inst->dma_cfg, NULL, &inst->dma_list_active_blank, true);

    // Setup internal Data Packet streams
    dvi_update_data_island_ptr(&inst->dma_list_vblank_sync,   &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_vblank_nosync, &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_active,        &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_error,         &inst->next_data_stream);
    dvi_update_data_island_ptr(&inst->dma_list_active_blank,  &inst->next_data_stream);

    inst->data_island_is_enabled  = true;
	//	inst->audio_enabled is set seperately
}

void __dvi_func(dvi_update_data_island_ptr)(struct dvi_scanline_dma_list *dma_list, data_island_stream_t *stream) {
    for (int i = 0; i < N_TMDS_LANES; ++i) {
        dma_cb_t *cblist = dvi_lane_from_list(dma_list, i);
        uint32_t *src = stream->data[i];

        if (i == TMDS_SYNC_LANE) {
            cblist[1].read_addr = src;
        } else {
            cblist[2].read_addr = src;
        }
    }
}

// video_freq: video sampling frequency = 
// audio_freq: audio sampling frequency
// CTS: Cycle Time Stamp  (32176 == 720x480)
// N: HDMI Constant
// 128 * audio_freq = video_freq * N / CTS
// e.g.: video_freq = 23495525, audio_freq = 44100 , CTS = 28000, N = 6272 
// CTS = (video_freq * N) / (audio_freq * 128)
void __dvi_func(dvi_set_audio_freq)(struct dvi_inst *inst, int audio_freq, int cts, int n) {
    inst->audio_freq = audio_freq;
    set_audio_clock_regeneration(&inst->audio_clock_regeneration, cts, n);
    set_audio_info_frame(&inst->audio_info_frame, audio_freq);
    uint pixelClock =   dvi_timing_get_pixel_clock(inst->timing);
    uint nPixPerFrame = dvi_timing_get_pixels_per_frame(inst->timing);
    uint nPixPerLine =  dvi_timing_get_pixels_per_line(inst->timing);
    inst->samples_per_frame  = (uint64_t)(audio_freq) * nPixPerFrame / pixelClock;
    inst->samples_per_line16 = (uint64_t)(audio_freq) * nPixPerLine * 65536 / pixelClock;
}

#if 0
void dvi_wait_for_valid_line(struct dvi_inst *inst) {
    uint32_t *tmdsbuf = NULL;
    queue_peek_blocking_u32(&inst->q_colour_valid, &tmdsbuf);
}
#endif


//	Called on the render core
bool __dvi_func(dvi_queue_audio_samples)(struct dvi_inst *inst, const int16_t* samples, int count)
{
	bool result = false;

	if (inst->audio_enabled)
	{
		if (count == 4) 
		{
			data_packet_t packet;
			data_island_streams_t* audio_streams;

			inst->audio_frame_count = set_audio_samples(&packet, samples, count, inst->audio_frame_count);

			if (queue_try_remove_u32(&inst->q_audio_streams_free, &audio_streams))
			{
				//	Compute the true and false versions of the stream, h_sync_polarity is constant
				encode_data_packet(&(audio_streams->stream_true), &packet, true, inst->timing->h_sync_polarity);
				encode_data_packet(&(audio_streams->stream_false), &packet, false, inst->timing->h_sync_polarity);

				queue_add_blocking_u32(&inst->q_audio_streams_valid, &audio_streams);

				result = true;
			}
		}
	}

	return result;
}

//	Called on the DVI core
void __dvi_func(dvi_update_data_stream)(struct dvi_inst *inst) {
	data_packet_t packet;
    bool vsync = inst->timing_state.v_state == DVI_STATE_SYNC;
	bool encode = false;

    if (inst->samples_per_frame != 0)
	{
		//	These are all infrequent
		if (inst->timing_state.v_state == DVI_STATE_FRONT_PORCH) 
		{
			if (inst->timing_state.v_ctr == 0) 
			{
				if (inst->dvi_frame_count & 1) 
				{
					packet = inst->avi_info_frame;
					encode = true;
				} 
				else 
				{
					packet = inst->audio_info_frame;
					encode = true;
				}
			} 
			else if (inst->timing_state.v_ctr == 1) 
			{
				packet = inst->audio_clock_regeneration;
				encode = true;
			}

			if (encode)
			{
				//	return the packet encoded, this doesn't happen often
				encode_data_packet(&inst->next_data_stream, &packet, inst->timing->v_sync_polarity == vsync, inst->timing->h_sync_polarity);
				return;
			}
			else
			{
				dvi_update_data_stream_null(inst);
				return;
			}
		}
		else if (inst->audio_enabled) 
		{
			//	Pull a stream from the queue, if there is one ready.
			//	Each packet is 4 samples at 44100Hz
			data_island_streams_t* audio_streams;
			if (queue_try_remove_u32(&inst->q_audio_streams_valid, &audio_streams))
			{
				//	Select the right pre-encoded stream
				if (inst->timing->v_sync_polarity == vsync)
					inst->next_data_stream = audio_streams->stream_true;
				else
					inst->next_data_stream = audio_streams->stream_false;

				queue_add_blocking_u32(&inst->q_audio_streams_free, &audio_streams);

				return;
			}
		}
	}
	
	//	By default, return a null stream
	dvi_update_data_stream_null(inst);
}

void __dvi_func(dvi_update_data_stream_null)(struct dvi_inst *inst) {
	bool vsync = inst->timing_state.v_state == DVI_STATE_SYNC;
	if (inst->timing->v_sync_polarity == vsync)	
		inst->next_data_stream = s_zero_stream_true;
	else
		inst->next_data_stream = s_zero_stream_false;
}

void __dvi_func(dvi_audio_enable)(struct dvi_inst *inst, bool enable)
{
	if (inst->data_island_is_enabled == true)
		inst->audio_enabled = enable;
}

#endif		//	FEATURE_A2_AUDIO