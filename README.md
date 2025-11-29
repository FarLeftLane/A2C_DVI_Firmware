# A2C_DVI Firmware: Digital Video for Apple IIc Computer

This is the firmware project for DVI/HDMI video cards for Apple IIc.
It directly produces a digital video stream from the Apple IIc's digital video output connector.
The signal is output via an HDMI connector, connecting the Apple IIc to modern displays with HDMI (or DVI) inputs.
This is a fork of the ADVI project specifically for the Apple IIc ( https://github.com/rallepalaveev/A2DVI )

# Release 1.8.3 supports the Laser computers!

To enable Laser support, you must set the Color/Mono button on the Laser to MONO (weird, right) and flash your A2CDVI device with the v1.8.3 update (see below).  Then when you power up the machine, press and hold the config button on your A2CDVI device until the configuration menus appear.  Using short and long presses, navigate to the "SET:" line and then slect the "MORE" option.   In the next screen, navigate to the "TYPE:" line and select "LASER".  The device will switch into Laser mode and colors/video should look correct.   If you move the device to a IIc, you will need to change the "TYPE:" setting back to IIC to get correct colors.

For firmware updates, download Releases/A2C_DVI_S_v1.8.3.uf2 ( https://github.com/FarLeftLane/A2C_DVI_Firmware/blob/master/Releases/A2C_DVI_S_v1.8.3.uf2 ) firmware.   Plug your adapter into your Mac/PC USB while holding down the BootSel button.  Drag the file to the Pico volume and it will reboot.  More details on flashing can be found here ( https://github.com/MrTechGadget/A2C_DVI_SMD ).  Do not HOTPLUG the A2CDVI to the IIc or Laser.  The v1.8 firmware will work on v2.0 hardware, but will not produce Apple IIc sounds.  It can be used with the "TEST TONE" option to test monitor/TV compatibility.

# Release 1.8.0 supports audio on v2.5 hardware!

To enable audio in the UI, press and hold the config button, select "MORE" on the "SET:" line.  Select "ON" on the "SOUND:" line and make sure to select "SAVE" to make is persistent.

For firmware updates, download Releases/A2C_DVI_S_v1.8.0.uf2 firmware.   Plug your adapter into your Mac/PC USB while holding down the BootSel button.  Drag the file to the Pico volume and it will reboot.  The v1.8 firmware will work on v2.0 hardware, but will not produce Apple IIc sounds.  It can be used with the "TEST TONE" option to test monitor/TV compatibility.

# To use this firmware, you need a A2CDVI card or an adapter for a A2DVI v2 card.

MrTechGadget's A2CDVI v2.5 adapter (supports audio): ( https://www.tindie.com/products/mrtechgadget/a2cdvi-v25/ )
Hardware design can be found here: ( https://github.com/MrTechGadget/A2C_DVI_SMD )

Crown Arcade Shop A2CDVI v2.0 (video only): ( https://crownarcade.co.kr/product/hdmi-adapter-for-apple-iic-iic/27/category/1/display/2/ )

Prototype A2C_DVI adapter (video only, requires a v2.x A2DVI card) ( https://github.com/FarLeftLane/A2C_DVI_Board )

# Configuration

Holding the Config button on your device while powering it on will select the defaults (note this turns off Audio and Laser)

Press and hold the Config button to access the config menus.  Navigate with short and long presses of the button.   Once you make changes, you need to select Save to make them persistant.

When the config menu isn't on the display, short taps on the Config button will cycle through different video modes and lines.  This is an easy way to switch to Black & White mode for things like A2Desktop or text editing.

# Building
This project is built using Visual Studio Code and the Raspberry Pi Pico extensions (Windows/Mac).  Install the extension (SDK 2.1.1) and open the main directory as the Folder for the project.  Select "Pico using compilers…" from the command palette and then select the A2C project from the command palette.  To “Run” a build, plug the Pico into your machine with the BootSel button down and click Run.

Mike Neil 8/17/2025

