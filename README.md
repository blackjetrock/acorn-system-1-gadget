# Acorn System One Gadget

This is a small gadget that attaches to an Acorn System One. It is a Pico XL RP2350 with 48 GPIOs attached to the
expansion bus of the System One

* 60K of memory. The RP2350 emulates RAM everywhere in the memory space apart from block 0
* Tracing of addresses as code executes. This is a crude trace at the moment, but could be expanded.
* Loading of binary files from SD card into memory. For instance the monitor can be loaded from SD card and run from RAM.
* Snapshot of the entire 64K memory to an SD card file.
* A USB CLI that can drive the gadget
* An OLED display and three keys, these do nothing at the moment.
* Reset from the USB CLI
* Powering of the System One from the USB supply of the Pico. The regulator has to be removed for this to work. Power can
  come from a host computer if the USB CLI is used, or a battery power pack for portability.


![IMG_20260319_062747278](https://github.com/user-attachments/assets/3a9ac897-2f96-40ce-a71c-dbb2aa8ba083)

