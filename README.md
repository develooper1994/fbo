# fbo
This software gets what is printed to framebuffer.
Special thanks to https://github.com/jwilk/fbcat repo!
VERSION: 1.0.0
-h <noarg> or --help <noarg> : print help
-v <noarg> or --version <noarg> : print the version
-d <arg> or --device <arg> : framebuffer device. Default: /dev/fb
-o <arg> or --output <arg> :
-g or --gray <noarg>
-c or --colored <noarg>
Don't mix color options!


## Example Usage
- ./fbo -g --output=screenshot.ppm
- ./fbo -c > screenshot.ppm
- ./fbo --device=/dev/fb -c --output=screenshot.ppm
- ./fbo --device=/dev/fb -g > screenshot.ppm

## Example Compilation
<path>/arm-poky-linux-gnueabi-gcc \
-mthumb -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a9 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security \
--sysroot=<sysroot-path> -O2 -o fbo main.c
