/*
MIT License

Copyright (c) 2025 Michael Neil, Far Left Lane

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

#include <stdio.h>
#include <pico/stdlib.h>
#include <string.h>
#include <hardware/pio.h>
#include <hardware/dma.h>
#include <hardware/timer.h>
#include <hardware/watchdog.h>
#include <pico/multicore.h>
#include "applebus/buffers.h"
#include "render/render.h"
#include "applebus/abus_pin_config.h"
#include "a2c_SEROUT.pio.h"
#ifdef FEATURE_A2_AUDIO
#include <hardware/adc.h>
#endif
#ifdef FEATURE_A2_AUDIO_PIO
#include "a2c_SND.pio.h"
#endif

#include "config/config.h"
#include "menu/menu.h"
#include "debug/debug.h"
#include "dvi/a2dvi.h"


#define NO_NTSC_LUT     1    //  If we need extra memory for testing

#include "hgrdecode_LUT.h"


//  Pins
#define PIN_SEROUT  0           //  A2C Pin 11 - A2E Pin 49 - GPIO 0
#define PIN_14M     1           //  A2C Pin 2  - A2E Pin 48 - GPIO 1
#define PIN_WNDW    2           //  A2C Pin 7  - A2E Pin 47 - GPIO 2
#define PIN_TEXT    3           //  A2C Pin 1  - A2E Pin 46 - GPIO 3
#define PIN_GR      4           //  A2C Pin 10 - A2E Pin 45 - GPIO 4
#define PIN_SND_PIO 5           //  A2C Pin 5  - A2E Pin 44 - GPIO 5
                                //  Ground GPIO 5 and 6 (A2E pins 44, 43) if not used
#define PIN_BUTTON  7           //  Button     - A2E Pin 42 - GPIO 7  10k pull down, 2K pull up

#define PIN_ENABLE  11              //  pull down GP11 to enable the 245 that controls D0-D7 (A2E PINs 49 - 42)
#define PIO_INPUT_PIN_BASE 0        //  SEROUT is 0
#define PIO_SND_INPUT_PIN_BASE 5    //  1VSND is 5 (PIN_SND_PIO)

#define PIN_ADC_SND 27              //  GPIO 27 is ADC1 and used for analog sound.  GPIO26 is used in the A2DVI designs.

//  WNDW IRQ data structures
uint64_t s_last_WNDW = 0;                       //  Last time we saw WNDW go low in microseconds
uint32_t s_scanline = 0;                        //  What scan line are we on (0-191) based on WNDW interrupts
struct repeating_timer s_repeating_timer;       //  We use a repeating timer to tell if WNDW has stopped
bool s_sync_found = false;                      //  We set this true in the WNDW handler and set it false when the timer doesn't see activity

//  PIO data structures
PIO s_pio;                                          //  The A2C PIO program
uint s_a2c_sm;                                      //  PIO state machine for SEROUT
uint s_a2c_snd_sm;                                  //  PIO state machine for SND

static uint32_t s_a2c_data = 0;
#ifdef FEATURE_A2_AUDIO
static uint32_t s_a2c_snd_data = 0;
static uint32_t s_a2c_snd_data_count = 0;
#endif

#define A2C_DATA_RX 0x00000001
#define A2C_SND_RX 0x00000002

uint32_t s_screen_buffer[192][18];                  //  Our buffer of the A2C screen, we don't use the A2 memory
bool s_screen_GR_buffer[192];                       //  See table below
bool s_screen_TEXT_buffer[192];                     //  See table below
                                                    //  TEXT and GR Pins
                                                    //  Mode:   TEXT    GR      HGR     DGR     DHGR
                                                    //  TEXT:   HIGH    LOW     LOW     HIGH    HIGH
                                                    //  GR:     LOW     HIGH    HIGH    HIGH    HIGH

bool s_menu_screen_init = false;                    //  We lazy init the menu screen once
bool s_show_menu_screen = false;                    //  Is the menu screen up

bool s_button_state = false;                        //  State of the button
uint64_t s_last_BUTTON = 0;                         //  Last state of the button to track transitions
const uint64_t s_long_button_press = (500 * 1000);  //  500,000 microseconds (half second)

uint64_t s_a2c_boot_time = 1;                       //  Keep track of the time spent waiting on A2C video data for debugging
uint64_t s_total_PIO_time = 1;
uint64_t s_blocking_PIO_time = 1;
uint64_t s_total_render_time = 1;
uint64_t s_render_time = 1;
uint32_t s_debug_value_1 = 0;
uint32_t s_debug_value_2 = 0;


//  We repurpose cfg_color_style, default is 2
//  We do this so that the config stored in flash doesn't change between firmware (A2DVI and A2C_DVI)
typedef enum {
    CS_A2DVI       = 2,     //  Default
    CS_CLAMP       = 1,
    CS_NTSC        = 0
} a2c_color_mode_t;

void __time_critical_func(WNDW_irq_callback)(uint gpio, uint32_t event_mask)
{
    uint64_t now = to_us_since_boot (get_absolute_time());          //  Record the time
    uint32_t delta = now - s_last_WNDW;

    //  If we have been low for more than 500 microseconds we are in the vertical blank (4.48332 miliseconds vs 39.12 microseconds)
    if ((delta > 500) && (s_scanline > 192))
        s_scanline = 0;
    
    s_last_WNDW = now;
    if (s_sync_found == false)
    {
        s_sync_found = true;                                        //  If we have not seen a sync (WNDW) mark it seen
    }
}

void __time_critical_func(wait_frame_start)()
{
    bool framestart = false;
    while (gpio_get(PIN_WNDW) == false)
    {
        // Wait till WNDW goes high
    }

    while (!framestart)
    {
        uint64_t startTime = to_us_since_boot (get_absolute_time());
        while (gpio_get(PIN_WNDW) == true)
        {
            uint64_t endTime = to_us_since_boot (get_absolute_time());

            uint32_t delta = endTime - startTime;
            
            //  if WNDW has been high for > 500 microseconds, we are in a blanking
            if (delta > 500)
            {
                framestart = true;
                return;
            }
        }
        
        while (gpio_get(PIN_WNDW) == false)
        {
            // Spin till low
        }
    }
}

bool __time_critical_func(repeating_timer_callback)(__unused struct repeating_timer *t)
{
    //  Get the current time;
    uint64_t now = to_us_since_boot (get_absolute_time());

    uint32_t delta = now - s_last_WNDW;

    if (delta > (1 * 1000 * 1000))
    {
        //  Its been more than a second since we saw WNDW activity, so mark sync as lost
        s_sync_found = false;

        //  Switch to the Splash screen
        bus_cycle_counter = 0;
    }

    return true;
}

//  Reboot the machine
bool s_needs_reboot = false;
bool s_save_required = false;

void DELAYED_COPY_CODE(software_reset)()
{
    //  Wait for the button to come up
    while (gpio_get(PIN_BUTTON) == true)
    {
        //  Spin
    }

    watchdog_enable(1, 1);
    while(1);
}

//  Utility routine to left justify text in the 40 Col menu screen
void DELAYED_COPY_CODE(leftY)(uint32_t y, const char* pMsg, TPrintMode PrintMode)
{
    uint32_t x = 0;
    while (pMsg[x] != 0)
        x++;
    x = 40-x;
    printXY(x, y, pMsg, PrintMode);
}

//  What mono mode should we display
static bool DELAYED_COPY_CODE(mono_command)(char * command_name, int index, bool update, bool selected)
{
    bool result = false;

    if (update == true)
    {
        if (index == 0)
            color_mode = COLOR_MODE_BW;
        else if (index == 1)
            color_mode = COLOR_MODE_GREEN;
        else if (index == 2)
            color_mode = COLOR_MODE_AMBER;
    }
    else
    {
        if ((index == 0) && (color_mode == COLOR_MODE_BW))
            result = true;
        else if ((index == 1) && (color_mode == COLOR_MODE_GREEN))
            result = true;
        else if ((index == 2) && (color_mode == COLOR_MODE_AMBER))
            result = true;
    }

    return result;
}

//  What color mode should we display, we repurpose cfg_color_style
static bool DELAYED_COPY_CODE(color_command)(char * command_name, int index, bool update, bool selected)
{
    bool result = false;

    if (update == true)
    {
        if (index == 0)
            cfg_color_style = CS_A2DVI;
        else if (index == 1)
            cfg_color_style = CS_NTSC;
        else if (index == 2)
            cfg_color_style = CS_CLAMP;
    }
    else
    {
        if ((index == 0) && (cfg_color_style == CS_A2DVI))
            result = true;
        else if ((index == 1) && (cfg_color_style == CS_NTSC))
            result = true;
        else if ((index == 2) && (cfg_color_style == CS_CLAMP))
            result = true;
    }

    return result;
}

//  Scanlines on / off
static bool DELAYED_COPY_CODE(scanline_command)(char * command_name, int index, bool update, bool selected)
{
    bool result = false;
    //  ENABLE DISABLE MONO

    if (update == true)
    {
        if (index == 0)
            cfg_scanline_mode = ScanlinesOn;
        else if (index == 1)
            cfg_scanline_mode = ScanlinesOff;
        else if (index == 2)
            cfg_scanline_mode = ScanlinesMonochrome;
    }
    else
    {
        if ((index == 0) && (cfg_scanline_mode == ScanlinesOn))
            result = true;
        else if ((index == 1) && (cfg_scanline_mode == ScanlinesOff))
            result = true;
        else if ((index == 2) && (cfg_scanline_mode == ScanlinesMonochrome))
            result = true;
    }

    return result;
}

//  Color or B&W
static bool DELAYED_COPY_CODE(mode_command)(char * command_name, int index, bool update, bool selected)
{
    bool result = false;
    //  Color B&W

    if (update == true)
    {
        if (index == 0)
        {
            SET_IFLAG(0, IFLAGS_FORCED_MONO);           //  Color
            cfg_rendering_fx = FX_NONE;
        }
        else if (index == 1)
        {
            SET_IFLAG(1, IFLAGS_FORCED_MONO);           //  Mono
            cfg_rendering_fx = FX_NONE;
        }
        else if (index == 2)
        {
            SET_IFLAG(0, IFLAGS_FORCED_MONO);           //  Mixed   Default
            cfg_rendering_fx = FX_ENABLED;
        }
    }
    else
    {
        mono_rendering = (internal_flags & IFLAGS_FORCED_MONO);

        if ((index == 0) && (mono_rendering == false) && (cfg_rendering_fx == FX_NONE))
            result = true;
        else if ((index == 1) && (mono_rendering == true) && (cfg_rendering_fx == FX_NONE))
            result = true;
        else if ((index == 2) && (cfg_rendering_fx == FX_ENABLED))
            result = true;
    }

    return result;
}

//  hdmi/dvi video size
static bool DELAYED_COPY_CODE(video_command)(char * command_name, int index, bool update, bool selected)
{
    bool result = false;

    if (update == true)
    {
        if (index == 0)
        {
            if (cfg_video_mode != Dvi720x480)
            {
                cfg_video_mode = Dvi720x480;
                s_needs_reboot = true;
                s_save_required = true;
            }
        }
        else if (index == 1)
        {
            if (cfg_video_mode != Dvi640x480)
            {
                cfg_video_mode = Dvi640x480;
                s_needs_reboot = true;
                s_save_required = true;
            }
        }
    }
    else
    {
        mono_rendering = (internal_flags & IFLAGS_FORCED_MONO);

        if ((index == 0) && (cfg_video_mode == Dvi720x480))
            result = true;
        else if ((index == 1) && (cfg_video_mode == Dvi640x480))
            result = true;
    }

    return result;
}

//  Save / Exit / Load defaults
static bool DELAYED_COPY_CODE(config_command)(char * command_name, int index, bool update, bool selected)
{
    bool result = false;
    //  Save, Default

    if (update == true)
    {
        if (index == 0)
        {
            //  Save
            config_save();

            if (s_needs_reboot)
                software_reset();

            //  Leave the screen
            s_show_menu_screen = false;
        }
        else if (index == 1)
        {
            //  Default
            DviVideoMode_t old_video_mode = cfg_video_mode;

            config_load_defaults();

            if (old_video_mode != cfg_video_mode)
            {
                s_needs_reboot = true;
                s_save_required = true;
            }
        }
        else if (index == 2)
        {
            if (IS_IFLAG(IFLAGS_DEBUG_LINES))               //  Toggle debug lines
            {
                SET_IFLAG(0, IFLAGS_DEBUG_LINES);
            }
            else
            {
                SET_IFLAG(1, IFLAGS_DEBUG_LINES);
            }
        }
    }
    else
    {
        if (index == 2)
            result = IS_IFLAG(IFLAGS_DEBUG_LINES);
    }

    return result;
}

//  Save / Exit / Load defaults
static bool DELAYED_COPY_CODE(exit_command)(char * command_name, int index, bool update, bool selected)
{
    bool result = false;
    //  EXIT

    if (update == true)
    {
        if (index == 0)
        {
            //  Exit
            if (s_save_required)
                config_save();

            if (s_needs_reboot)
                software_reset();

            //  Leave the screen
            s_show_menu_screen = false;
        }
    }

    return result;
}

typedef bool (*menu_command_callback_t)(char * command_name, int index, bool update, bool selected);

struct menu_command
{
    char* command_name;
    menu_command_callback_t command;
};

struct menu_commands
{
    char * command_header;
    struct menu_command commands[3];
};

char DELAYED_COPY_DATA(a2c_TitleFirmware)[] =
    "A2C_DVI - FIRMWARE V" FW_VERSION;

    char DELAYED_COPY_DATA(a2c_TitleCopyright)[] =
    "THORSTEN BREHM, RALLE PALAVEEV";


const uint s_menu_line_start = 7;
const uint s_menu_header_left = 6;
const uint s_menu_column_left[3] = {8, 17, 26};

int s_menu_cursor_X = 0;
int s_menu_cursor_Y = 0;
bool s_menu_cursor_vertical_direction = true;

struct menu_commands DELAYED_COPY_DATA(a2c_menu_items_main)[] = 
{
    { "MODE:", { {"COLOR", mode_command }, {"MONO", mode_command }, {"MIXED", mode_command } } },
    { "", { {"", NULL }, {"", NULL }, {"", NULL } } },
    { "COLOR:", { {"A2DVI", color_command }, {"NTSC", color_command }, {"CLAMP", color_command } } },
    { "", { {"", NULL }, {"", NULL }, {"", NULL } } },
    { "MONO:", { {"B&W", mono_command }, {"GREEN", mono_command }, {"AMBER", mono_command } } },
    { "", { {"", NULL }, {"", NULL }, {"", NULL } } },
    { "LINES:", { {"ENABLE", scanline_command }, {"DISABLE", scanline_command }, {"MONO", scanline_command } } },
    { "", { {"", NULL }, {"", NULL }, {"", NULL } } },
    { "VIDEO:", { {"720X480", video_command }, {"640X480", video_command }, {"", NULL } } },
    { "", { {"", NULL }, {"", NULL }, {"", NULL } } },
    { "SET:", { {"SAVE", config_command }, {"DEFAULT", config_command }, {"DEBUG", config_command } } },
    { "", { {"", NULL }, {"", NULL }, {"", NULL } } },
    { "EXIT", { {"", exit_command }, {"", NULL }, {"", NULL } } }
};

uint32_t a2c_menu_items_main_size = sizeof(a2c_menu_items_main) / sizeof(a2c_menu_items_main[0]);

struct menu_commands* s_current_menu_screen = a2c_menu_items_main;
uint32_t s_current_menu_screen_size = sizeof(a2c_menu_items_main) / sizeof(a2c_menu_items_main[0]);

//  0x1F = Inverse undersscore, 0x20 = Inverse space, colors are Low Res Colors
const uint8_t s_init_menu_text[24] =   { 0x20, 0x1F, 0x20, 0x20, 0x5C, 0x20, 0x1F, 0x20, 0x1F, 0x20, 0x1F, 0x20, 0x1F, 0x20, 0x1F, 0x20, 0x1F, 0x20, 0x1F, 0x20, 0x1F, 0x20, 0x20, 0x20 };
const uint8_t s_init_menu_colors[24] = { 0x30, 0x30, 0x70, 0x70, 0x70, 0x70, 0x70, 0x10, 0x10, 0x90, 0x90, 0xB0, 0xB0, 0xE0, 0xE0, 0x60, 0x60, 0x30, 0x30, 0xF0, 0xF0, 0x70, 0x70, 0x70 };

#define BGCOLOR (2)
#define TEXT_OFFSET(line) ((((line) & 0x7) << 7) + ((((line) >> 3) & 0x3) * 40))

static void DELAYED_COPY_CODE(init_menu_screen)(void)
{
    // initialize the screen buffer area, we use the Apple II shadow text screen

    // set colors
    for (uint8_t line = 0; line < 24; line++)
    {        
        for (uint8_t i = 0; i < 40; i++)
        {
            //  p1 are the glyphs
            text_p1[TEXT_OFFSET(line) + i] = 0xA0;      //  Space

            //  p3 are the colors upper nibble is foreground color
            text_p3[TEXT_OFFSET(line) + i] = s_init_menu_colors[line];
        }
    }

    for (int line = 0; line < 24; line++)
    {
        text_p1[TEXT_OFFSET(line) + 0] = s_init_menu_text[line];
        text_p1[TEXT_OFFSET(line) + 1] = s_init_menu_text[line];
        text_p1[TEXT_OFFSET(line) + 2] = s_init_menu_text[line];
        text_p1[TEXT_OFFSET(line) + 3] = s_init_menu_text[line];
    }
    
    for (uint8_t i = 4; i < 40; i++)
    {
        //  p1 are the glyphs
        text_p1[TEXT_OFFSET(3) + i] = 0x9F;     //  Underscore
        text_p1[TEXT_OFFSET(4) + i] = 0x5C;     //  Double score (mouse text)
        text_p1[TEXT_OFFSET(5) + i] = 0x4C;     //  Over score (mouse text)
    }

    printXY(0, 3, " ", PRINTMODE_NORMAL);
    printXY(0, 4, " ", PRINTMODE_NORMAL);
    printXY(0, 5, " ", PRINTMODE_NORMAL);

    leftY( 2, a2c_TitleFirmware, PRINTMODE_NORMAL);

    leftY(21, "A2C_DVI (C) 2025", PRINTMODE_NORMAL);
    leftY(22, "MIKE NEIL/CHRIS AUGER/JOSHUA CLARK", PRINTMODE_NORMAL);
    leftY(23, a2c_TitleCopyright, PRINTMODE_NORMAL);
}

static void DELAYED_COPY_CODE(draw_menu_screen)(void)
{
    for (int i = 0; i < sizeof(a2c_menu_items_main) / sizeof(a2c_menu_items_main[0]); i++) 
    {
        if ((s_menu_cursor_X == 0) && (s_menu_cursor_Y == i))
        {
            printXY(s_menu_header_left, s_menu_line_start + i, a2c_menu_items_main[i].command_header, PRINTMODE_FLASH);
        }
        else
        {
            printXY(s_menu_header_left, s_menu_line_start + i, a2c_menu_items_main[i].command_header, PRINTMODE_NORMAL);
        }

        for (int j = 0; j < 3; j++)
        {
            TPrintMode printMode = PRINTMODE_NORMAL;
            bool selected = false;

            if (a2c_menu_items_main[i].commands[j].command != NULL)
                selected = (*a2c_menu_items_main[i].commands[j].command)(a2c_menu_items_main[i].commands[j].command_name, j, false, false);
            
            if (selected)
                printMode = PRINTMODE_INVERSE;

            if ((s_menu_cursor_Y == i) && (s_menu_cursor_X == (j+1)))
                printMode = PRINTMODE_FLASH;

            printXY(s_menu_header_left + s_menu_column_left[j], s_menu_line_start + i, a2c_menu_items_main[i].commands[j].command_name, printMode);
        }
    }
}

static void DELAYED_COPY_CODE(show_menu)(void)
{
    s_menu_cursor_X = 0;
    s_menu_cursor_Y = 0;
    s_menu_cursor_vertical_direction = true;
}

static void DELAYED_COPY_CODE(menu_short_press)(void)
{
    if (s_menu_cursor_vertical_direction == true)
    {
        s_menu_cursor_Y++;

        while (a2c_menu_items_main[s_menu_cursor_Y].commands[0].command == NULL)
        {
            s_menu_cursor_Y = s_menu_cursor_Y + 1;

            if (s_menu_cursor_Y >= sizeof(a2c_menu_items_main) / sizeof(a2c_menu_items_main[0]))
            {
                s_menu_cursor_Y = 0;
                break;
            }
        }
    }
    else
    {
        s_menu_cursor_X++;

        while (a2c_menu_items_main[s_menu_cursor_Y].commands[s_menu_cursor_X - 1].command == NULL)
        {
            s_menu_cursor_X = s_menu_cursor_X + 1;

            if (s_menu_cursor_X > 3)
            {
                s_menu_cursor_X = 0;
                break;
            }
        }
    }

    //  Bounds checks
    s_menu_cursor_X = s_menu_cursor_X % 4;
    s_menu_cursor_Y = s_menu_cursor_Y % (sizeof(a2c_menu_items_main) / sizeof(a2c_menu_items_main[0]));
}

static void DELAYED_COPY_CODE(menu_long_press)(void)
{
    if (s_menu_cursor_vertical_direction == true)
    {
        if (s_menu_cursor_X == 0)
        {
            //  Are we on a header command (EXIT), these have no text in the first command but do have a command
            if ( (a2c_menu_items_main[s_menu_cursor_Y].commands[0].command != NULL) && (a2c_menu_items_main[s_menu_cursor_Y].commands[0].command_name[0] == 0) )
            {
                (*a2c_menu_items_main[s_menu_cursor_Y].commands[0].command)(a2c_menu_items_main[s_menu_cursor_Y].commands[0].command_name, 0, true, true);
            }
            else
            {
                //  We are on the header, transition to the commands and horizonal
                s_menu_cursor_vertical_direction = false;
                s_menu_cursor_X = 1;
            }
        }
    }
    else
    {
        if (s_menu_cursor_X == 0)
        {
            // If we are on a header, transition back to vertical
            s_menu_cursor_vertical_direction = true;
        }
        else
        {
            //  Execute a setting
            if (a2c_menu_items_main[s_menu_cursor_Y].commands[s_menu_cursor_X - 1].command != NULL)
            {
                (*a2c_menu_items_main[s_menu_cursor_Y].commands[s_menu_cursor_X - 1].command)(a2c_menu_items_main[s_menu_cursor_Y].commands[s_menu_cursor_X - 1].command_name, (s_menu_cursor_X - 1), true, true);
            }

            // And return to the header
            s_menu_cursor_X = 0;
            s_menu_cursor_vertical_direction = true;
        }
    }
}

int s_menu_cycle_count = 0;     //  Keep track of what display mode we are on

static void DELAYED_COPY_CODE(cycle_video_modes)()
{
    s_menu_cycle_count++;
    s_menu_cycle_count = s_menu_cycle_count % 8;

    switch (s_menu_cycle_count)
    {
        case 0:
            mode_command("COLOR", 0, true, false);
            mono_command("B&W", 0, true, false);
            scanline_command("DISABLE", 1, true, false);
            break;

        case 1:
            mode_command("MONO", 1, true, false);
            mono_command("B&W", 0, true, false);
            scanline_command("DISABLE", 1, true, false);
            break;

        case 2:
            mode_command("MONO", 1, true, false);
            mono_command("GREEN", 1, true, false);
            scanline_command("DISABLE", 1, true, false);
            break;

        case 3:
            mode_command("MONO", 1, true, false);
            mono_command("AMBER", 2, true, false);
            scanline_command("DISABLE", 1, true, false);
            break;

        case 4:
            mode_command("COLOR", 0, true, false);
            mono_command("B&W", 0, true, false);
            scanline_command("ENABLE", 0, true, false);
            break;

        case 5:
            mode_command("MONO", 1, true, false);
            mono_command("B&W", 0, true, false);
            scanline_command("ENABLE", 0, true, false);
            break;

        case 6:
            mode_command("MONO", 1, true, false);
            mono_command("GREEN", 1, true, false);
            scanline_command("ENABLE", 0, true, false);
            break;

        case 7:
            mode_command("MONO", 1, true, false);
            mono_command("AMBER", 2, true, false);
            scanline_command("ENABLE", 0, true, false);
            break;

        default:
            s_menu_cycle_count = 0;                         //  Shouldn't hit this
    }
}

static bool s_long_press_processed = false;

void DELAYED_COPY_CODE(process_button)()
{
    bool button = gpio_get(PIN_BUTTON);

    //  button pressed, start the timers
    uint64_t now = to_us_since_boot(get_absolute_time());
    if ((button != s_button_state) && (button == true))
    {
        s_last_BUTTON = now;
        s_long_press_processed = false;
    }

    uint64_t delta = now - s_last_BUTTON;

    if ((button == true) && (s_long_press_processed == false))
    {
        //  A press has occured is it a long press?
        if (delta > s_long_button_press)
        {
            if (s_show_menu_screen == true)
            {
                menu_long_press();
            }
            else
            {
                //  Show the menu screen on a first press
                s_show_menu_screen = true;
                show_menu();
            }

            s_long_press_processed = true;
        }
    }

    if (button != s_button_state)
    {
        if (button == false)
        {
            //  A release has occured

            if ((delta < s_long_button_press) && (delta > 1000))       //  Debounce, 1000 microseconds
            {
                //  Short press
                if (s_show_menu_screen == true)
                {
                    menu_short_press();
                }
                else
                {
                    //  If we are not in the menu, cycle the video modes based on the short button press
                    cycle_video_modes();
                }
            }

            s_long_press_processed = false;
        }

        //  Record the neww state and time
        s_button_state = button;
    }
}


void DELAYED_COPY_CODE(clear_a2c_debug_monitor)(void)
{
    // clear status lines
    for (uint i = 0; i < sizeof(status_line)/4; i++)
    {
        ((uint32_t*)status_line)[i] = 0xA0A0A0A0;
    }    
}

bool s_debug_monitor_cleared = false;
char s_temp_line_buffer[40];

void DELAYED_COPY_CODE(update_a2c_debug_monitor)(void)
{
    if (s_debug_monitor_cleared == false)
    {
        //  Clear the debug monitor text lines
        clear_a2c_debug_monitor();
        s_debug_monitor_cleared = true;
    }

#if 0
    uint8_t* line3 = &status_line[80];
    if ((frame_counter & 3) == 0)           // do not update too fast, so data remains readable
    {
        int2hex(&line3[18], s_debug_value_1, 8);
        int2hex(&line3[18+9], s_debug_value_2, 8);
    }

#else

    uint8_t* line1 = status_line;
    uint8_t* line2 = &status_line[40];
    uint8_t* line3 = &status_line[80];
    uint8_t* line4 = &status_line[120];

    if ((frame_counter & 3) == 0)           // do not update too fast, so data remains readable
    {

        uint32_t total = s_total_PIO_time / 1000;
        uint32_t blocking = s_blocking_PIO_time;        //  times 1000 100.0
        uint32_t blocking_percentage = 1100;            //  Error value, 110.0%
        if (total != 0)
            blocking_percentage = blocking / total;

        copy_str(&line1[0], "PIO %: ");
        int2str(blocking_percentage, s_temp_line_buffer, 4);
        copy_str(&line1[7], s_temp_line_buffer);

        total = s_total_render_time / 1000;
        blocking = s_render_time;                       //  times 1000 100.0
        blocking_percentage = 1100;                     //  Error value, 110.0%
        if (total != 0)
            blocking_percentage = blocking / total;

        copy_str(&line1[7+4+1], "RND %: ");
        int2str(blocking_percentage, s_temp_line_buffer, 4);
        copy_str(&line1[7+4+1+7], s_temp_line_buffer);

        copy_str(&line1[7+4+1+7+4+1], "Free: ");
        int2str(getFreeHeap(), s_temp_line_buffer, 6);
        copy_str(&line1[7+4+1+7+4+1+6], s_temp_line_buffer);  

        copy_str(&line2[0], "Frame: ");
        int2str(frame_counter, s_temp_line_buffer, 8);
        copy_str(&line2[0+8], s_temp_line_buffer);

#ifdef FEATURE_A2_AUDIO
        copy_str(&line2[7+4+1+7+4+1], "Snd: ");
        float time_f = s_total_PIO_time / 1000000.0;
        float snd_rate = 110.0;
        if (time_f != 0.0)
            snd_rate = (float)s_a2c_snd_data_count / time_f;
        uint32_t snd_rate_int = snd_rate;
        int2str(snd_rate_int, s_temp_line_buffer, 6);
        copy_str(&line2[7+4+1+7+4+1+6], s_temp_line_buffer);
#endif

        if (cfg_video_mode == Dvi720x480)
        {
            copy_str(&line3[0], "720x480");
        } else if (cfg_video_mode == Dvi720x480)
        {
            copy_str(&line3[0], "640x480");
        }

        int2hex(&line3[18], s_debug_value_1, 8);
        int2hex(&line3[18+9], s_debug_value_2, 8);
    }


    if ((frame_counter & 0x0F) == 0)         // do not update too fast, so data remains readable
    {
        //  Display a bit of screen data
        int2hex(&line4[0], s_screen_buffer[2][0], 8);
        int2hex(&line4[9], s_screen_buffer[2][1], 8);
        int2hex(&line4[18], s_screen_buffer[2][2], 8);
        int2hex(&line4[18+9], s_screen_buffer[2][3], 8);
    }
#endif
}

void DELAYED_COPY_CODE(render_a2c_debug)(bool IsVidexMode, bool top)
{
    uint8_t color_mode = 0;

    if (top)
    {
        if (!IS_IFLAG(IFLAGS_DEBUG_LINES))
        {
            //  If no debugging, just render black at the top of the screen
            for (uint row = 0; row < 16; row++)
            {
                dvi_get_scanline(tmdsbuf);
                dvi_scanline_rgb640(tmdsbuf, tmdsbuf_red, tmdsbuf_green, tmdsbuf_blue);

                for (uint32_t x = 0; x < 320; x++)
                {
                    *(tmdsbuf_red++)   = TMDS_SYMBOL_0_0;
                    *(tmdsbuf_green++) = TMDS_SYMBOL_0_0;
                    *(tmdsbuf_blue++)  = TMDS_SYMBOL_0_0;
                }

                dvi_send_scanline(tmdsbuf);
            }
        }
        else
        {
            update_a2c_debug_monitor();

            // render two debug monitor lines above the screen area
            uint8_t* line1 = status_line;
            uint8_t* line2 = &status_line[40];
            render_text40_line(line1, 0, color_mode);
            render_text40_line(line2, 0, color_mode);
        }
    }
    else
    {
        if (!IS_IFLAG(IFLAGS_DEBUG_LINES))
        {
            //  If no debugging, just render black at the top of the screen
            for (uint row = 0; row < 16; row++)
            {
                dvi_get_scanline(tmdsbuf);
                dvi_scanline_rgb640(tmdsbuf, tmdsbuf_red, tmdsbuf_green, tmdsbuf_blue);

                for (uint32_t x = 0; x < 320; x++)
                {
                    *(tmdsbuf_red++)   = TMDS_SYMBOL_0_0;
                    *(tmdsbuf_green++) = TMDS_SYMBOL_0_0;
                    *(tmdsbuf_blue++)  = TMDS_SYMBOL_0_0;
                }

                dvi_send_scanline(tmdsbuf);
            }
        }
        else
        {
            // render two debug monitor lines below the screen area
            uint8_t* line3 = &status_line[80];
            uint8_t* line4 = &status_line[120];
            render_text40_line(line3, 0, color_mode);
            render_text40_line(line4, 0, color_mode);
        }
    }
}

//  These are the render modes that are supported.
typedef enum {
    RM_BW          = 0,
    RM_A2DVI       = 1,
    RM_NTSC        = 2,
    RM_CLAMP       = 3
} a2c_render_mode_mode_t;

static void DELAYED_COPY_CODE(render_a2c_full_line)(a2c_render_mode_mode_t render_mode, uint line)        //  volatile uint32_t screen_buffer[192][18]
{
    dvi_get_scanline(tmdsbuf);                                              //  We only spend about 0.2% of the tim,e blocking
    dvi_scanline_rgb(tmdsbuf, tmdsbuf_red, tmdsbuf_green, tmdsbuf_blue);

    uint64_t start_time = to_us_since_boot (get_absolute_time());

    uint32_t left_margin = ((dvi_x_resolution - (32 * 18)) / 8) * 2;        //  We want this to always be even.  18 32-bit samples of SEROUT
    uint32_t right_margin = ((32 * 18) / 2) + left_margin;

    //  Fill in the left and right margins, this needs to be done first for timing reasons
    for(uint i = 0; i < left_margin; i++)
    {
        *(tmdsbuf_red+(right_margin))   = TMDS_SYMBOL_0_0;
        *(tmdsbuf_green+(right_margin)) = TMDS_SYMBOL_0_0;
        *(tmdsbuf_blue+(right_margin))  = TMDS_SYMBOL_0_0;
        *(tmdsbuf_red++)   = TMDS_SYMBOL_0_0;
        *(tmdsbuf_green++) = TMDS_SYMBOL_0_0;
        *(tmdsbuf_blue++)  = TMDS_SYMBOL_0_0;
    }

    if (render_mode == RM_BW) //  mono_rendering
    {
        uint8_t color_offset = color_mode * 12;                 //  BW, Green, Amber, etc


        for(uint i = 0; i < 18; i++)
        {
            // Load in the first 32 dots
            uint32_t dots = s_screen_buffer[line][i];
            uint32_t next_dots = (i < 17) ? s_screen_buffer[line][i+1] : 0;
            
            // Consume 32 dots, two at a time
            for(uint j = 0; j < 16; j++)
            {
                //  pixels need to be reversed
                uint32_t dot = ((dots >> 29) & 0x02) | ((dots >> 31) & 0x01);      // second highest bit or'd with highest bit and swapped (highest becomes lowest)
                uint32_t coffset = color_offset + ((dot) & 0x03);

                *(tmdsbuf_red++)   = tmds_mono_pixel_pair[coffset + 0];
                *(tmdsbuf_green++) = tmds_mono_pixel_pair[coffset + 4];
                *(tmdsbuf_blue++)  = tmds_mono_pixel_pair[coffset + 8];
                dots <<= 2;

                //  Add 16 more dots
                if (j == 7)
                    dots = (dots & 0xFFFF0000) | (next_dots >> 16);
            }
        }
    }
    else if (render_mode == RM_A2DVI)
    {
        // Each hires byte contains 7 pixels which may be shifted right 1/2 a pixel. That is
        // represented here by 14 'dots' to precisely describe the half-pixel positioning.
        //
        // For each pixel, inspect a window of 8 dots around the pixel to determine the
        // precise dot locations and colors.
        //
        // Dots would be scanned out to the CRT from MSB to LSB (left to right here):
        //
        //            previous   |        next
        //              dots     |        dots
        //        +-------------------+--------------------------------------------------+
        // dots:  | 31 | 30 | 29 | 28 | 27 | 26 | 25 | 24 | 23 | ... | 14 | 13 | 12 | ...
        //        |              |         |              |
        //        \______________|_________|______________/
        //                       |         |
        //                       \_________/
        //                         current
        //                          pixel

        //  We are rendering using the A2DVI LUT
        uint oddness = 0;
        uint dot_count = 0;

        //  Due to the encoding, we shift the color part of the screen to the right to align with the B&W text
        //  This is a timing bug.  I can't seem to do this fix in 640 mode without the video breaking up
        //  I think reformating the LUTs might help

        if (cfg_video_mode == Dvi720x480)
        {
            dot_count = 2;

            *(tmdsbuf_red++)   = TMDS_SYMBOL_0_0;
            *(tmdsbuf_green++) = TMDS_SYMBOL_0_0;
            *(tmdsbuf_blue++)  = TMDS_SYMBOL_0_0;
        }

        for(uint i = 0; i < 18; i++)
        {
            // Load in the first 32 dots
            uint32_t dots = s_screen_buffer[line][i];
            uint32_t next_dots = (i < 17) ? s_screen_buffer[line][i+1] : 0;

            // Consume 32 dots, two at a time
            for(uint j = 0; j < 16; j++)
            {
                if (dot_count < (32 * 18))      //  buffer is 18 32-bit dots
                {
                    //  Render HGR
                    uint dot_pattern = oddness | ((dots >> 24) & 0xff);                                 //  Total of 9 bits, oddness is phase mod 2 due to dual pixels

                    *(tmdsbuf_red++)   = tmds_hires_color_patterns_red[dot_pattern];                       
                    *(tmdsbuf_green++) = tmds_hires_color_patterns_green[dot_pattern];
                    *(tmdsbuf_blue++)  = tmds_hires_color_patterns_blue[dot_pattern];

                    dots <<= 2;
                    dot_count = dot_count + 2;
                    oddness ^= 0x100;
                }

                //  Consume 16 more dots
                if (j == 7)
                    dots = (dots & 0xFFFF0000) | (next_dots >> 16);
            }
        }
    }
#ifndef NO_NTSC_LUT
    else if (render_mode == RM_NTSC)
    {
        //  We are rendering using a 11 bit (NUM_CAP 8 to 4) NTSC style color LUT
        uint oddness = 0;       //  oddness is real just phase, but only 0 or 2 due to douple pixels

        //  Due to the NTSC encoding, we shift the color part of the screen to the right to align with the B&W text
        uint dot_count = 4;

        *(tmdsbuf_red++)   = TMDS_SYMBOL_0_0;
        *(tmdsbuf_green++) = TMDS_SYMBOL_0_0;
        *(tmdsbuf_blue++)  = TMDS_SYMBOL_0_0;

        *(tmdsbuf_red++)   = TMDS_SYMBOL_0_0;
        *(tmdsbuf_green++) = TMDS_SYMBOL_0_0;
        *(tmdsbuf_blue++)  = TMDS_SYMBOL_0_0;

        for(uint i = 0; i < 18; i++)
        {
            // Load in the first 32 dots
            uint32_t dots = s_screen_buffer[line][i];
            uint32_t next_dots = (i < 17) ? s_screen_buffer[line][i+1] : 0;
            
            // Consume 32 dots, two at a time
            for(uint j = 0; j < 16; j++)
            {
                if (dot_count < (32 * 18))      //  buffer is 18 32-bit dots
                {
                    //  Render DHGR, this inner loop is very timing dependant, too slow and hdmi breaks up
                    uint dot_pattern = (oddness << 2) | ((dots >> 22) & 0x3ff);                                 //  Total of 11 bits
                    
                    *(tmdsbuf_red++)   = tmds_hgrdecode_NTSC_8to4_LUT_color_patterns_red[dot_pattern];
                    *(tmdsbuf_green++) = tmds_hgrdecode_NTSC_8to4_LUT_color_patterns_green[dot_pattern];
                    *(tmdsbuf_blue++)  = tmds_hgrdecode_NTSC_8to4_LUT_color_patterns_blue[dot_pattern];
                    
                    dots <<= 2;
                    dot_count = dot_count + 2;
                    oddness ^= 0x100;
                }

                //  Consume 16 more dots
                if (j == 7)
                    dots = (dots & 0xFFFF0000) | (next_dots >> 16);
            }
        }
    }
#endif      //  NO_NTSC_LUT
    else if ((render_mode == RM_CLAMP) || (render_mode == RM_NTSC))
    {
        //  We are rendering using a 9 bit (NUM_CAP 8 to 3, Clamped) NTSC style color LUT
        uint oddness = 0;
        uint dot_count = 0;

        //  Due to the NTSC encoding, we shift the color part of the screen to the right to align with the B&W text
        //  This is a timing bug.  I can't seem to do this fix in 640 mode without the video breaking up
        //  I think reformating the LUTs might help
        
        if (cfg_video_mode == Dvi720x480)
        {
            dot_count = 2;
            
            *(tmdsbuf_red++)   = TMDS_SYMBOL_0_0;
            *(tmdsbuf_green++) = TMDS_SYMBOL_0_0;
            *(tmdsbuf_blue++)  = TMDS_SYMBOL_0_0;
        }

        for(uint i = 0; i < 18; i++)
        {
            // Load in the first 32 dots
            uint32_t dots = s_screen_buffer[line][i];
            uint32_t next_dots = (i < 17) ? s_screen_buffer[line][i+1] : 0;

            // Consume 32 dots, two at a time
            for(uint j = 0; j < 16; j++)
            {
                if (dot_count < (32 * 18))      //  buffer is 18 32-bit dots
                {
                    //  Render DHGR, this inner loop is very timing dependant, too slow and hdmi breaks up
                    uint dot_pattern = oddness | ((dots >> 24) & 0xff);                                 //  Total of 9 bits

                    *(tmdsbuf_red++)   = tmds_hgrdecode8to3_LUT_color_patterns_red[dot_pattern];
                    *(tmdsbuf_green++) = tmds_hgrdecode8to3_LUT_color_patterns_green[dot_pattern];
                    *(tmdsbuf_blue++)  = tmds_hgrdecode8to3_LUT_color_patterns_blue[dot_pattern];
                    
                    dots <<= 2;
                    dot_count = dot_count + 2;
                    oddness ^= 0x100;
                }

                //  Consume 16 more dots
                if (j == 7)
                    dots = (dots & 0xFFFF0000) | (next_dots >> 16);
            }
        }
    }

    uint64_t end_time = to_us_since_boot (get_absolute_time());
    s_total_render_time = end_time - s_a2c_boot_time;
    s_render_time = s_render_time + (end_time - start_time);

    dvi_send_scanline(tmdsbuf);             //  We spend about 0.4% waiting on the queu
}



void DELAYED_COPY_CODE(render_a2c)()
{
    //  Update the button state and selections
    process_button();

    // set flag when monochrome rendering is requested
    mono_rendering = (internal_flags & IFLAGS_FORCED_MONO);

    if (s_show_menu_screen)
    {
        if (s_menu_screen_init == false)
        {
            //  Init the menu screen, we use the shadowed text screen beacuse it isn't used.
            init_menu_screen();

            s_menu_screen_init = true;
        }

        draw_menu_screen();

        //  We need mouse text for the header
        soft_switches |= SOFTSW_ALTCHAR;

        //  Render the rest as text
        for (uint line = 0; line < 6; line++)                   //  0-7
        {
            render_color_text40_line(line);
        }

        //  We need flashing for the lower
        soft_switches &= ~SOFTSW_ALTCHAR;

        //  Render the rest as text                             // 48 (6) - 191
        for (uint line = 6; line < 24; line++)
        {
            render_color_text40_line(line);
        }
    }
    else if (s_sync_found)
    {
        //  Normal rendering, either mono or color

        if (mono_rendering == true)
        {
            for(uint line = 0; line < 192; line++)
            {
                //  Force mono mode
                render_a2c_full_line(RM_BW, line);
            }
        }
        else
        {
            a2c_render_mode_mode_t render_mode = RM_A2DVI;      //  s_screen_TEXT_buffer[line], s_screen_GR_buffer[line],
                
            if (cfg_color_style == CS_A2DVI)
                render_mode = RM_A2DVI;
            else if (cfg_color_style == CS_NTSC)
                render_mode = RM_NTSC;
            else if (cfg_color_style == CS_CLAMP)
                render_mode = RM_CLAMP;

            for(uint line = 0; line < 192; line++)
            {
                if (cfg_rendering_fx == FX_ENABLED)                 //  Mixed text and graphics, B&W for Text, Color for graphics
                {
                    if (s_screen_GR_buffer[line] == false)
                        render_mode = RM_BW;
                }

                render_a2c_full_line(render_mode, line);
            }
        }
    }
    else
    {
        //  We don't hane a valid WNDW, so we display an error screen
        s_menu_screen_init = false;

        // initialize the screen buffer area, we use the Apple II shadow text screen
        clearTextScreen();

        centerY(19, "TECHNICAL DIFFICULTIES", PRINTMODE_FLASH);
        centerY(21, "APPLE //c POWER OFF?", PRINTMODE_NORMAL);

         //  Render the rest as text
         for (uint line = 0; line < 24; line++)
         {
             render_color_text40_line(line);
         }
    }
}


void __time_critical_func(a2c_init)()
{
#ifdef FEATURE_A2_AUDIO
    adc_init();
#endif

    //  init and turn off pins we don't need
    gpio_init(CONFIG_PIN_APPLEBUS_PHI0);
    gpio_set_pulls(CONFIG_PIN_APPLEBUS_PHI0, false, false);

    // initialize GPIO on all 8 data pins + SELECT + RW
    for(int pin = CONFIG_PIN_APPLEBUS_DATA_BASE; pin < CONFIG_PIN_APPLEBUS_DATA_BASE + 10; pin++)
    {
        gpio_init(pin);
        gpio_set_pulls(pin, false, false);
    }
    
    //  Setup the BUTTON pin, we can't do an interupt on this, so we need to poll
    gpio_init(PIN_BUTTON);
    gpio_set_dir(PIN_BUTTON, GPIO_IN);

    //  Setup the GR pin
    gpio_init(PIN_GR);
    gpio_set_dir(PIN_GR, GPIO_IN);

    //  Setup the TEXT pin
    gpio_init(PIN_TEXT);
    gpio_set_dir(PIN_TEXT, GPIO_IN);
    
    //  Interrupt on each scan line     11500 per second
    gpio_init(PIN_WNDW);
    gpio_set_dir(PIN_WNDW, GPIO_IN);

    //  Setup PIN_SND_PIO as input for debugging
    gpio_init(PIN_SND_PIO);
    gpio_set_dir(PIN_SND_PIO, GPIO_IN);

    //  Pull the enable pin low so we can data throught the 245
    gpio_init(PIN_ENABLE);
    gpio_set_dir(PIN_ENABLE, GPIO_OUT);
    gpio_pull_down(PIN_ENABLE);

    //  Initalize the button state
    s_button_state = gpio_get(PIN_BUTTON);
    s_last_BUTTON = to_us_since_boot (get_absolute_time());

    if (s_button_state == true)
        config_load_defaults();                 //  A2C_DVI  Force defaults if button is down at boot

#ifdef FEATURE_A2_AUDIO
    adc_gpio_init(PIN_ADC_SND);                         //  GPIO27 for analog sound input
    adc_select_input(1);                                //  GPIO27 is ADC1
    adc_set_round_robin(0x01 << 1);                     //  Bit 1 is ADC1
    adc_irq_set_enabled(false);                         //  No IRQ
    adc_set_clkdiv(135.055406);                         //  48MHz / 44.1KHz / 1088.435374,  1087.40 gives the best, 8x over sampling (352800 is 135.055406)
    adc_fifo_setup(true, false, 0, false, false);       //  enable = true, DMA = false, 0 dma, no errors, no byte shift
    adc_run(true);                                      //  enable free running mode
#endif

    // PIO setup
    // Load the a2c_input program, and configure a free state machine to run the program.
    s_pio = pio0;
    int a2c_offset = pio_add_program(s_pio, &a2c_input_program);
#ifdef FEATURE_A2_AUDIO_PIO
    uint snd_offset = pio_add_program(s_pio, &a2c_snd_input_program);
#endif
    s_a2c_sm = pio_claim_unused_sm(s_pio, true);
#ifdef FEATURE_A2_AUDIO_PIO
    s_a2c_snd_sm = pio_claim_unused_sm(s_pio, true);
#endif

    //  Setup a repeating timer to keep an eye on WNDW an see if it has stopped
    add_repeating_timer_ms(500, repeating_timer_callback, NULL, &s_repeating_timer);

    //  Wait for the start frame, the splash screen will be up until then
    wait_frame_start();
    s_last_WNDW = to_us_since_boot (get_absolute_time());

    //  Enable rendering once we see the first frame
    soft_switches = SOFTSW_HIRES_MODE | SOFTSW_V7_MODE3;

    //  Enable the interupt
    gpio_set_irq_enabled_with_callback(PIN_WNDW, GPIO_IRQ_EDGE_FALL, true, WNDW_irq_callback);      //  Interrupt on WNDW going low

    //  Start the PIO program
    a2c_input_program_init(s_pio, s_a2c_sm, a2c_offset, PIO_INPUT_PIN_BASE);
#ifdef FEATURE_A2_AUDIO_PIO
    a2c_snd_input_program_init(s_pio, s_a2c_snd_sm, snd_offset, PIO_SND_INPUT_PIN_BASE);
#endif
}

#ifdef FEATURE_A2_AUDIO
// Audio Related

static uint s_sample_count = 0;

#ifdef FEATURE_A2_AUDIO_PIO
/*
    PIN_SND_PIO is read at 32x 44100Hz (1.4112 MHz) as a 1 bit input by PIO, 32-bits at a time from the FIFO.  These 32 bits are considered sub samples
*/

int16_t s_last_sample = 0;


//  Return a signed sample value
int16_t square_process_sound_sub_samples(uint32_t sub_sample_data)
{
    if (sub_sample_data == 0)
        s_last_sample = -2000;
    else
        s_last_sample = 2000;

    return s_last_sample;
}

bool __time_critical_func(add_sound_sample_PIO)(uint32_t snd_data) 
{
    if (dvi0.audio_ring.buffer == NULL)
        return false;

    int size = get_write_size(&dvi0.audio_ring, false);
    if (size > 0)
    {
        audio_sample_t *audio_ptr = get_write_pointer(&dvi0.audio_ring);
        if (audio_ptr != NULL)
        {
            audio_sample_t sample;

            sample.channels[0] = square_process_sound_sub_samples(snd_data);

            sample.channels[1] = sample.channels[0];

            s_debug_value_1 = (uint32_t)audio_ptr;
            
            *audio_ptr = sample;
            s_sample_count++;

            increase_write_pointer(&dvi0.audio_ring, 1);
        }
        else
        {
            s_debug_value_2++;
            __mem_fence_release();
        }
    }
    else
    {
        s_debug_value_2++;
        __mem_fence_release();
    }

    return true;
}

#endif

uint32_t s_snd_high = 0;
uint32_t s_snd_low = 0;
uint32_t s_snd_sum = 0;
uint32_t s_sub_sample_count = 0;
int32_t s_sub_sample_value = 0;


//  Return a signed sample value
int16_t process_sound_sub_samples_eight_x(uint32_t sub_sample_data)
{    
    int16_t sample = ((int16_t)sub_sample_data) - 2000;                   //  samples seem to be from 0-2048 (272 - 1712)
    
    s_sub_sample_value = s_sub_sample_value + sample;
    s_sub_sample_count++;

    if (s_sub_sample_count % 8 == 0)
    {
        sample = s_sub_sample_value / 8;
        s_sub_sample_value = 0;
    }

    return sample;
}

bool __time_critical_func(add_sound_sample)(uint32_t snd_data) 
{
    if (dvi0.audio_ring.buffer == NULL)
        return false;

    int16_t sound_sample = process_sound_sub_samples_eight_x(snd_data);

    if (s_sub_sample_count % 8 == 0)
    {
        int size = get_write_size(&dvi0.audio_ring, false);
        if (size > 0)
        {
            audio_sample_t *audio_ptr = get_write_pointer(&dvi0.audio_ring);
            if (audio_ptr != NULL)
            {
                audio_sample_t sample;

                sample.channels[0] = sound_sample;

                sample.channels[1] = sample.channels[0];

                if (snd_data > s_snd_high)
                    s_snd_high = snd_data;
                
                if (s_snd_low == 0)
                    s_snd_low = snd_data;
                
                if (snd_data < s_snd_low)
                    s_snd_low = snd_data;

                *audio_ptr = sample;
                s_sample_count++;
                s_snd_sum = s_snd_sum + snd_data;

                if ((s_sample_count % (44100 * 5)) == 0)
                {
                    // s_debug_value_1 = s_snd_sum / s_sample_count;
                    // s_debug_value_2 = s_snd_high - s_snd_low;
                    s_snd_sum = 0;
                    s_sample_count = 0;

                    s_snd_high = snd_data;
                    s_snd_low = snd_data;
                }

                increase_write_pointer(&dvi0.audio_ring, 1);
            }
            else
            {
                __mem_fence_release();
            }
        }
        else
        {
            __mem_fence_release();
        }
    }

    return true;
}

#endif			//	FEATURE_A2_AUDIO

uint32_t __time_critical_func(pio_get_multiple)(bool block)
{
    uint32_t result = 0;
    uint64_t start_time = to_us_since_boot (get_absolute_time());

    while (result == 0)
    {
        if (pio_sm_is_rx_fifo_empty(s_pio, s_a2c_sm) == false)
        {
            s_a2c_data = pio_sm_get(s_pio, s_a2c_sm);
            result |= A2C_DATA_RX;
        }

#ifdef FEATURE_A2_AUDIO_PIO
        if (pio_sm_is_rx_fifo_empty(s_pio, s_a2c_snd_sm) == false)
        {
            s_a2c_snd_data = pio_sm_get(s_pio, s_a2c_snd_sm);
            s_a2c_snd_data_count++;
            result |= A2C_SND_RX;
        }
#endif

#ifdef FEATURE_A2_AUDIO
        if (adc_fifo_is_empty() == false)
        {
            s_a2c_snd_data = adc_fifo_get();
            s_a2c_snd_data_count++;
            result |= A2C_SND_RX;
        }
#endif
        if (block == false)
            break;
    }

    uint64_t end_time = to_us_since_boot (get_absolute_time());
    s_total_PIO_time = end_time - s_a2c_boot_time;
    s_blocking_PIO_time = s_blocking_PIO_time + (end_time - start_time);

    return result;
}

void __time_critical_func(a2c_loop)()
{
    // initialize the Apple IIc interface
    a2c_init();

    s_a2c_boot_time = to_us_since_boot (get_absolute_time());

    //  Turn on debug lines
    // SET_IFLAG(1, IFLAGS_DEBUG_LINES);

    int x = 0;      //  These are the a2c "screen" indexes
    int y = 0;

    //  Loop forever reading from the PIO RX queue
    while (true) 
    {
        uint32_t rxflags = pio_get_multiple(true);
        
        if ((rxflags & A2C_DATA_RX) != 0)
        {
            uint32_t rxdata = s_a2c_data;
            // uint32_t rxdata = pio_sm_get_blocking(s_pio, s_a2c_sm);

            if (x == 0)
            {
                //  Delay reading s_scanline until we have the first bytes, this is updated in the WNDW interrupt handler
                y = s_scanline % 192;
                s_scanline++;
                
                //  record the state of the GR pin to know if this is a color or B&W line
                s_screen_GR_buffer[y] = gpio_get(PIN_GR);
                s_screen_TEXT_buffer[y] = gpio_get(PIN_TEXT);
            }
            
            //  SEROUT is inverted from memory bits
            s_screen_buffer[y][x] = ~rxdata;

            //  We read 18 *32 = 576 bits per line
            x = (x + 1) % 18;

            //  We increment this just so the diagnostics show there is activity
            bus_cycle_counter++;
        }
#ifdef FEATURE_A2_AUDIO
        if ((rxflags & A2C_SND_RX) != 0)
        {
            //  Process some audio when 
            if (a2dvi_started() == true)
                add_sound_sample(s_a2c_snd_data);
        }
#endif
    }
}
