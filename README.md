# Debugprobe and 3 CDC's on Pico


Interim solution and framework for extended functionality with additional CDCs while preserving the programming interface for openOCD. With planned functional enhancements, the existing working structure should be retained for now to make it easy to follow updates of the main branch. One modification involves disabling the CMSIS-DAP interface in order to activate only the added optional functionality. - WIP!
- The implemented UART interfaces are a slightly stripped-down copy version of the original RB 'uart.c' version. The naming of the serial interface was initially reduced to letters to avoid duplicate numbering with 0 and 1. The acronym chosen in the end provides a clear distinction and serves as an example. ( [ATE](https://en.wikipedia.org/wiki/Automatic_test_equipment), [DUT](https://en.wikipedia.org/wiki/Device_under_test) ) - WIP


.


<img width="1004" height="238" alt="image" src="https://github.com/user-attachments/assets/48838bc0-2623-4559-a824-d591a9154e27" />



.


---

Firmware source for the Raspberry Pi Debug SWD/UART as accessory to be run on a Raspberry Pi Pico or Pico 2.

-

[Raspberry Pi Pico product page](https://www.raspberrypi.com/products/raspberry-pi-pico/)

[Raspberry Pi Pico 2 product page](https://www.raspberrypi.com/products/raspberry-pi-pico-2/)

# Documentation

Debug Probe documentation can be found at the [Raspberry Pi Microcontroller Documentation portal](https://www.raspberrypi.com/documentation/microcontrollers/debug-probe.html#about-the-debug-probe).

# Hacking

For the purpose of making changes or studying of the code, you may want to compile the code yourself.

First, clone the repository:
```
git clone https://github.com/raspberrypi/debugprobe
cd debugprobe
```
Initialize and update the submodules:
```
 git submodule update --init --recursive
```
Then create and switch to the build directory:
```
 mkdir build
 cd build
```
If your environment doesn't contain `PICO_SDK_PATH`, then either add it to your environment variables with `export PICO_SDK_PATH=/path/to/sdk` or add `-DPICO_SDK_PATH=/path/to/sdk` to the arguments to CMake below.

Run cmake and build the code:
```
 cmake ..
 make
```
Done! You should now have a `debugprobe.uf2` that you can upload to your Debug Probe via the UF2 bootloader.

## Building for the Pico 1

If you want to create the version that runs on the Pico, then you need to invoke `cmake` in the sequence above with the `DEBUG_ON_PICO=ON` option:
```
cmake -DDEBUG_ON_PICO=ON ..
```
This will build with the configuration for the Pico and call the output program `debugprobe_on_pico.uf2`, as opposed to `debugprobe.uf2` for the accessory hardware.

Note that if you first ran through the whole sequence to compile for the Debug Probe, then you don't need to start back at the top. You can just go back to the `cmake` step and start from there.

## Building for the Pico 2

If using an existing debugprobe clone:
- You must completely regenerate your build directory, or use a different one.
- You must also sync and update submodules.
- `PICO_SDK_PATH` must point to a version 2.0.0 or greater install.

```
git submodule sync
git submodule update --init --recursive
mkdir build-pico2
cd build-pico2
cmake -DDEBUG_ON_PICO=1 -DPICO_BOARD=pico2 ../
```
This will build with the configuration for the Pico 2 and call the output program `debugprobe_on_pico2.uf2`.

...
