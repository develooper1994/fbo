# fbo
This software captures what printed to framebuffer.
Software only supports pbm(P4), pgm(P5) and ppm(P6) image formats.
Special thanks to https://github.com/jwilk/fbcat repo!
VERSION: 1.0.6
-h or --help <noarg> : print help
-v or --version <noarg> : print the version
-d or --device <arg> : framebuffer device. Default: /dev/fb
-o or --output <arg> :
-g or --gray <noarg>
-c or --colored <noarg>
-b or --colored <noarg> : bitmap file format otherwise file format is pgm or ppm
-t or --thread <noarg> : Use all cores of the processor. It may affect on multicore systems on bigger screens. (only PPM for now)
Don't mix color options!

## NetPBM Viewer
[NetPBM](https://kylepaulsen.com/stuff/NetpbmViewer/)

## Example Usage
- ./fbo -g --output=screenshot.pgm
- ./fbo -c > screenshot.ppm
- ./fbo --device=/dev/fb -c --output=screenshot.ppm
- ./fbo --device=/dev/fb -g > screenshot.pgm

## Example Makefiles
- https://github.com/develooper1994/fbo/blob/main/Makefile
    - make CC=arm-linux-gnueabi-gcc // change it as you wish

- https://github.com/develooper1994/fbo/blob/main/fbo.pro

## Example Commanline Compilation
(path)/arm-poky-linux-gnueabi-gcc \
-mthumb -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a9 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security \
--sysroot=(sysroot-path) -pthread -O3 -o fbo main.c
