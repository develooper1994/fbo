# fbo
This software captures what printed to framebuffer.\
Software supports netpbm(P4,P5,P6)(pbm,pgm,ppm) image formatsand also bmp colored(bgr channel order) and grayscale image formats.\
Note: Framebuffer channel order is BGR but netpbm channel order is RGB!Special thanks to https://github.com/jwilk/fbcat repo!\
VERSION: 1.1.0\
-h or --help <noarg> : print help\
-v or --version <noarg> : print the version\
-d or --device <arg> : framebuffer device. Default: /dev/fb\
-o or --output <arg> : output file\
-g or --gray <noarg> : grayscale color mode. P5, pgm file format. RGB channel order\
-c or --colored <noarg> : full color mode. P6, ppm file format\
-b or --colored <noarg> : bitmap file format otherwise file format is pgm or ppm\
-t or --thread <noarg> : Use all cores of the processor. It may affect on multicore systems on bigger screens. (only PGM and PPM for now)\
Don't mix color options!\

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
    - change "target.path" as you wish

## Example Commanline Compilation
(path)/arm-poky-linux-gnueabi-gcc \
-mthumb -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a9 -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security -Werror=format-security \
--sysroot=(sysroot-path) -pthread -O3 -o fbo main.c
