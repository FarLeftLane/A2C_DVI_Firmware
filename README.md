# A2C_DVI Firmware: Digital Video for Apple IIc Computer

This is the firmware project for DVI/HDMI video cards for Apple IIc.
It directly produces a digital video stream from the Apple IIc's digital video output connector.
The signal is output via an HDMI connector, connecting the Apple IIc to modern displays with HDMI (or DVI) inputs.
This is a fork of the ADVI project specifically for the Apple IIc ( https://github.com/rallepalaveev/A2DVI )

# Release 1.8.0 supports audio on v2.5 hardware!

To enable audio in the UI, press and hold the config button, select "MORE" on the "SET:" line.  Select "ON" on the "SOUND:" line and make sure to select "SAVE" to make is persistent.

For firmware updates, download Releases/A2C_DVI_S_v1.8.0.uf2 firmware.   Plug your adapter into your Mac/PC USB while holding down the BootSel button.  Drag the file to the Pico volume and it will reboot.  The v1.8 firmware will work on v2.0 hardware, but will not produce Apple IIc sounds.  It can be used with the "TEST TONE" option to test monitor/TV compatibility.

# To use this firmware, you need a A2CDVI card or an adapter for a A2DVI v2 card.

MrTechGadget's A2CDVI v2.5 adapter (supports audio): ( https://www.tindie.com/products/mrtechgadget/a2cdvi-v25/ )

Crown Arcade Shop A2CDVI v2.0 (video only): ( https://crownarcade.co.kr/product/hdmi-adapter-for-apple-iic-iic/27/category/1/display/2/ )

Prototype A2C_DVI adapter (video only, requires a v2.x A2DVI card) ( https://github.com/FarLeftLane/A2C_DVI_Board )

# Building
This project is built using Visual Studio Code and the Raspberry Pi Pico extensions (Windows/Mac).  Install the extension (SDK 2.1.1) and open the main directory as the Folder for the project.  Select "Pico using compilers…" from the command palette and then select the A2C project from the command palette.  To “Run” a build, plug the Pico into your machine with the BootSel button down and click Run.

Mike Neil 8/17/2025

