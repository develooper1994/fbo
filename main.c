#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>

#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>

#include <linux/fb.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>


typedef struct fb_fix_screeninfo fsi;
typedef struct fb_var_screeninfo vsi;
typedef struct fb_cmap cmap;

// wingdi-bitmap structure document
#pragma pack(push, 1)
typedef struct {
  uint16_t bfType;
  uint32_t bfSize;
  uint16_t bfReserved1;
  uint16_t bfReserved2;
  uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
  uint32_t biSize;
  int32_t  biWidth;
  int32_t  biHeight;
  uint16_t biPlanes;
  uint16_t biBitCount;
  uint32_t biCompression;
  uint32_t biSizeImage;
  int32_t  biXPelsPerMeter;
  int32_t  biYPelsPerMeter;
  uint32_t biClrUsed;
  uint32_t biClrImportant;
} BITMAPINFOHEADER;
#pragma pack(pop)

// BMP file header
static BITMAPFILEHEADER bfh = {
    .bfType = 0x4D42,  // 'BM'
    // .bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size,
    .bfReserved1 = 0,
    .bfReserved2 = 0,
    .bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
};

#if !defined(le32toh) || !defined(le16toh)

#if BYTE_ORDER == LITTLE_ENDIAN
#define le32toh(x) (x)
#define le16toh(x) (x)
#else
#include <byteswap.h>
#define le32toh(x) bswap_32(x)
#define le16toh(x) bswap_16(x)
#endif

#endif

#define VERSION_MAJOR "1"
#define VERSION_MINOR "0.0"
#define VERSION VERSION_MAJOR "." VERSION_MINOR
#define INTRO "This software captures what printed to framebuffer. \n" \
              "Software only supports pbm(P4), pgm(P5) and ppm(P6) image formats. \n" \
              "Special thanks to https://github.com/jwilk/fbcat repo!"
#define defaultFbDev "/dev/fb"
#define defaultOutputFile stdout
#define Author "* Author: Mustafa Selçuk Çağlar\n"
#define BugTrackerUrl "https://github.com/develooper1994/fbo/issues"

// file types
#define PBM "pbm"
#define PGM "pgm"
#define PPM "ppm"
#define BMP "bmp"

// exit codes
#define EXIT_POSIX_ERROR 2
#define EXIT_NOT_SUPPORTED 3
#define EXIT_HELP 4


static inline void posixError(const char *s, ...) {
    va_list argv;
    va_start(argv, s);
    fprintf(stderr, "fbo: ");
    vfprintf(stderr, s, argv);
    fprintf(stderr, ": ");
    perror(NULL);
    va_end(argv);
    exit(EXIT_POSIX_ERROR);
}
static inline void notSupported(const char *s) {
    fprintf(stderr,
            "fbo: not yet supported: %s\n"
            "Please file a bug at <%s>.\n",
            s, BugTrackerUrl);
    exit(EXIT_NOT_SUPPORTED);
}
static inline void printHelp(){
    printf("\n"
           INTRO "\n"
           "VERSION: " VERSION "\n"
           "-h <noarg> or --help <noarg> : print help \n"
           "-v <noarg> or --version <noarg> : print the version \n"
           "-d <arg> or --device <arg> : framebuffer device. Default: " defaultFbDev "\n"
           "-o <arg> or --output <arg> :  \n"
           "-g or --gray <noarg> \n"
           "-c or --colored <noarg> \n"
           "Don't mix color options! \n"
           );
    exit(EXIT_HELP);
}


static inline uint8_t getColor(uint32_t pixel, const struct fb_bitfield *bitfield,
                               uint16_t *colormap) {
    return colormap[(pixel >> bitfield->offset) & ((1 << bitfield->length) - 1)] >> 8;
}

static inline uint8_t getGrayscale(uint32_t pixel, const vsi *info,
                                   const cmap *colormap) {
    const uint8_t red = colormap->red[(pixel >> info->red.offset) & ((1 << info->red.length) - 1)] >> 8;
    const uint8_t green = colormap->green[(pixel >> info->green.offset) & ((1 << info->green.length) - 1)] >> 8;
    const uint8_t blue = colormap->blue[(pixel >> info->blue.offset) & ((1 << info->blue.length) - 1)] >> 8;
    return (uint8_t)(0.3 * red + 0.59 * green + 0.11 * blue);
}

static inline uint8_t reverseBits(uint8_t b) {
    /* reverses the order of the bits in a byte
   * from
   * https://graphics.stanford.edu/~seander/bithacks.html#ReverseByteWith64BitsDiv
   *
   * how it works:
   *
   *   w = 0bABCDEFGH
   *   x = w * 0x0202020202
   *     = 0bABCDEFGHABCDEFGHABCDEFGHABCDEFGHABCDEFGH0
   *   y = x & 0x010884422010
   *     = 0bABCDEFGHABCDEFGHABCDEFGHABCDEFGHABCDEFGH0
   *     & 0b10000100010000100010000100010000000010000
   *     = 0bA0000F000B0000G000C0000H000D00000000E0000
   *     = (A << 40) + (B << 31) + (C << 22) + (D << 13) + (E << 4) + (F << 35)
   * + (G << 26) + (H << 17) z = y % 1023 = = (A << 0) + (B << 1) + (C << 2) +
   * (D << 3) + (E << 4) + (F << 5) + (G << 6) + (H << 7) = 0bHGFEDCBA
   */
    return (b * 0x0202020202ULL & 0x010884422010ULL) % 1023;
}

static inline void dumpVideoMemoryPbm(const uint8_t *video_memory,
                                         const vsi *info, bool black_is_zero,
                                         uint32_t line_length, FILE *fp) {
    // pbm(portable bitmap) -> P1, P4
    const uint32_t bytes_per_row = (info->xres + 7) / 8;
    uint8_t *row = (uint8_t *)malloc(bytes_per_row);
    if (row == NULL)
        posixError("malloc failed");
    assert(row != NULL);

    if (info->xoffset % 8)
        notSupported("xoffset not divisible by 8 in 1 bpp mode");
    fprintf(fp, "P4 %" PRIu32 " %" PRIu32 "\n", info->xres, info->yres);
    for (uint32_t y = 0; y < info->yres; y++) {
        const uint8_t *current =
            video_memory + (y + info->yoffset) * line_length + info->xoffset / 8;
        for (uint32_t x = 0; x < bytes_per_row; x++) {
            row[x] = reverseBits(*current++);
            if (black_is_zero)
                row[x] = ~row[x];
        }
        if (fwrite(row, 1, bytes_per_row, fp) != bytes_per_row)
            posixError("write error");
    }

    free(row);
}

static void dumpVideoMemoryPgm(const uint8_t *video_memory,
                                     const vsi *info,
                                     const struct fb_cmap *colormap,
                                     uint32_t line_length, FILE *fp) {
    // pgm(portable graymap) -> P2, P5
    const uint32_t bytes_per_pixel = (info->bits_per_pixel + 7) / 8;
    uint8_t *row = (uint8_t *)malloc(info->xres);
    if (row == NULL)
        posixError("malloc failed");
    assert(row != NULL);

    fprintf(fp, "P5 %" PRIu32 " %" PRIu32 " 255\n", info->xres, info->yres);
    for (uint32_t y = 0; y < info->yres; y++) {
        const uint8_t *current = video_memory + (y + info->yoffset) * line_length +
                  info->xoffset * bytes_per_pixel;
        for (uint32_t x = 0; x < info->xres; x++) {
            uint32_t pixel = 0;
            switch (bytes_per_pixel) {
            case 4:
                pixel = le32toh(*((uint32_t *)current));
                current += 4;
                break;
            case 2:
                pixel = le16toh(*((uint16_t *)current));
                current += 2;
                break;
            default:
                for (uint32_t i = 0; i < bytes_per_pixel; i++) {
                    pixel |= current[0] << (i * 8);
                    current++;
                }
                break;
            }
            row[x] = getGrayscale(pixel, info, colormap);
        }
        if (fwrite(row, 1, info->xres, fp) != info->xres)
            posixError("write error");
    }

    free(row);
}

static inline void dumpVideoMemoryPpm(const uint8_t *video_memory, const vsi *info,
                                   const cmap *colormap, uint32_t line_length,
                                   FILE *fp) {
    // ppm(portable pixelmap) -> P3, P6
    const uint32_t bytes_per_pixel = (info->bits_per_pixel + 7) / 8;
    uint8_t *row = (uint8_t *)malloc(info->xres * 3);
    if (row == NULL)
        posixError("malloc failed");
    assert(row != NULL);

    fprintf(fp, "P6 %" PRIu32 " %" PRIu32 " 255\n", info->xres, info->yres);
    for (uint32_t y = 0; y < info->yres; y++) {
        const uint8_t *current = video_memory + (y + info->yoffset) * line_length +
                  info->xoffset * bytes_per_pixel;
        for (uint32_t x = 0; x < info->xres; x++) {
            uint32_t pixel = 0;
            switch (bytes_per_pixel) {
            // The following code assumes little-endian byte ordering in the framebuffer.
            case 4:
                pixel = le32toh(*((uint32_t *)current));
                current += 4;
                break;
            case 2:
                pixel = le16toh(*((uint16_t *)current));
                current += 2;
                break;
            default:
                for (uint32_t i = 0; i < bytes_per_pixel; i++) {
                    pixel |= current[0] << (i * 8);
                    current++;
                }
                break;
            }
            row[x * 3 + 0] = getColor(pixel, &info->red, colormap->red);
            row[x * 3 + 1] = getColor(pixel, &info->green, colormap->green);
            row[x * 3 + 2] = getColor(pixel, &info->blue, colormap->blue);
        }
        if (fwrite(row, 1, info->xres * 3, fp) != info->xres * 3)
            posixError("write error");
    }

    free(row);
}

static void dumpVideoMemoryBmpColored(
    const uint8_t *video_memory,
    const vsi *info,
    const cmap *colormap,
    uint32_t line_length,
    FILE *fp
    ){

    const uint32_t bytes_per_pixel = (info->bits_per_pixel + 7) / 8;
    uint8_t *row = (uint8_t *)malloc(info->xres);
    if (row == NULL)
        posixError("malloc failed");
    assert(row != NULL);

    // Calculate the size of the image data (with padding)
    uint32_t row_size = (info->xres * 3 + 3) & (~3); // 3 byte per pixel
    uint32_t image_size = row_size * info->yres;
    // BMP file header
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size;

    // BMP info header
    BITMAPINFOHEADER bih;
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = info->xres;
    bih.biHeight = -info->yres;  // Negative height to store the image top-down
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = 0;
    bih.biSizeImage = image_size;
    bih.biXPelsPerMeter = 0;
    bih.biYPelsPerMeter = 0;
    bih.biClrUsed = 0;
    bih.biClrImportant = 0; // all colors are important! no shades gonna be saved

    // Write headers
    fwrite(&bfh, sizeof(BITMAPFILEHEADER), 1, fp);
    fwrite(&bih, sizeof(BITMAPINFOHEADER), 1, fp);

    for (uint32_t y = 0; y < info->yres; ++y) {
        const uint8_t *current = video_memory + (y + info->yoffset) * line_length + info->xoffset * bytes_per_pixel;
        for (uint32_t x = 0; x < info->xres; ++x) {
            uint32_t pixel = 0;
            switch (bytes_per_pixel) {
            case 4:
                pixel = le32toh(*((uint32_t *) current));
                current += 4;
                break;
            case 2:
                pixel = le16toh(*((uint16_t *) current));
                current += 2;
                break;
            default:
                for (uint32_t i = 0; i < bytes_per_pixel; i++) {
                    pixel |= current[0] << (i * 8);
                    current++;
                }
                break;
            }
            row[x * 3 + 0] = getColor(pixel, &info->red, colormap->red);
            row[x * 3 + 1] = getColor(pixel, &info->green, colormap->green);
            row[x * 3 + 2] = getColor(pixel, &info->blue, colormap->blue);
        }
        if (fwrite(row, 1, row_size, fp) != row_size) {
            posixError("write error");
        }
    }
}

static void dumpVideoMemoryBmpGrayscale(
  const uint8_t *video_memory,
  const vsi *info,
  const cmap *colormap,
  uint32_t line_length,
  FILE *fp
)
{
  const uint32_t bytes_per_pixel = (info->bits_per_pixel + 7) / 8;
  uint8_t *row = (uint8_t *)malloc(info->xres);
  if (row == NULL)
    posixError("malloc failed");
  assert(row != NULL);

  // Calculate the size of the image data (with padding)
  uint32_t row_size = (info->xres + 3) & (~3); // 1 byte per pixel
  uint32_t image_size = row_size * info->yres;
  // BMP file header
  bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size;

  // BMP info header
  BITMAPINFOHEADER bih;
  bih.biSize = sizeof(BITMAPINFOHEADER);
  bih.biWidth = info->xres;
  bih.biHeight = -info->yres;  // Negative height to store the image top-down
  bih.biPlanes = 1;
  bih.biBitCount = 8;
  bih.biCompression = 0;
  bih.biSizeImage = image_size;
  bih.biXPelsPerMeter = 0;
  bih.biYPelsPerMeter = 0;
  bih.biClrUsed = 256;
  bih.biClrImportant = 256;

  // Write headers
  fwrite(&bfh, sizeof(BITMAPFILEHEADER), 1, fp);
  fwrite(&bih, sizeof(BITMAPINFOHEADER), 1, fp);

  // Write the grayscale palette (256 shades of gray)
  for (int32_t i = 0; i < 256; i++)
  {
    uint8_t color[4] = {i, i, i, 0};
    fwrite(color, sizeof(color), 1, fp);
  }

  for (uint32_t y = 0; y < info->yres; y++)
  {
    const uint8_t *current = video_memory + (y + info->yoffset) * line_length + info->xoffset * bytes_per_pixel;
    for (uint32_t x = 0; x < info->xres; x++)
    {
      uint32_t pixel = 0;
      switch (bytes_per_pixel)
      {
        case 4:
          pixel = le32toh(*((uint32_t *) current));
          current += 4;
          break;
        case 2:
          pixel = le16toh(*((uint16_t *) current));
          current += 2;
          break;
        default:
          for (uint32_t i = 0; i < bytes_per_pixel; i++)
          {
            pixel |= current[0] << (i * 8);
            current++;
          }
          break;
      }
      row[x] = getGrayscale(pixel, info, colormap);
    }
    if (fwrite(row, 1, row_size, fp) != row_size) {
      posixError("write error");
    }
  }

  free(row);
}


int main(int argc, char **argv){
    // init
    char *fbdev_name = defaultFbDev;
    int fd, fd_ouput_file;
    FILE *ouput_file = defaultOutputFile;
    bool mmapped_memory = false, is_mono = false, black_is_zero = false;
    fsi fix_info;
    vsi var_info;
    uint16_t colormap_data[4][1 << 8];
    cmap colormap = {
        0,
        1 << 8,
        colormap_data[0],
        colormap_data[1],
        colormap_data[2],
        colormap_data[3],
    };

    /// get info from user // getopt_long
    int result_opt;
    int option_index = 0;
    int flag_help = 0, flag_version = 0, flag_device = 0, flag_output = 0, flag_gray = 0, flag_colored = 0, flag_err = 0;
    char *output_file_name;

    /*
    if (isatty(STDOUT_FILENO)) {
        fprintf(stderr, "fbo: refusing to write binary data to a terminal\n");
        flag_err = 1;
    }
    */

    // Kısa ve Uzun seçenekleri tanımlama
    static const char* short_options = "hvd:o:gc";
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"device", optional_argument, 0, 'd'},
        {"output", optional_argument, 0, 'o'},
        {"gray", no_argument, 0, 'g'},
        {"colored", no_argument, 0, 'c'},
        {0, 0, 0, 0}
    };

    // Seçenekleri ayrıştırma
    while ((result_opt = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
        switch (result_opt) {
        case 'h':
            flag_help = 1;
            break;
        case 'v':
            flag_version = 1;
            break;
        case 'd':
            flag_device = 1;
            fbdev_name = optarg;
            break;
        case 'o':
            flag_output = 1;
            output_file_name = optarg;
            break;
        case 'g':
            flag_gray = 1;
            break;
        case 'c':
            flag_colored = 1;
            break;
        case '?':
            // error part
            if (optopt == 'd')
                fprintf(stderr, "option -d or --device without argument!. Device " defaultFbDev "\n");
            else if (optopt == 'o')
                fprintf(stderr, "option -o or --output without argument!...\n");
            else if (optopt != 0)
                fprintf(stderr, "invalid option: -%c\n", optopt);
            else
                fprintf(stderr, "invalid long option!...\n");
            /* fprintf(stderr, "invalid long option: %s\n", argv[optind - 1]); */
        default:
            flag_err = 1;
            break;
        }
    }

    // Printing help message
    if(flag_err || flag_help){
        printHelp();
        exit(flag_err ? EXIT_FAILURE : EXIT_SUCCESS);
    }
    // Printing versiyon message
    if (flag_version) {
        printf("VERSION: " VERSION "\n");
        exit(EXIT_SUCCESS);
    }

    // Device and Output checks
    if(!flag_device){
        fbdev_name = getenv("FRAMEBUFFER");
        if (fbdev_name == NULL || fbdev_name[0] == '\0')
            fbdev_name = defaultFbDev;
        fprintf(stderr,"Framebuffer device: %s\n", fbdev_name);
    }
    if ((fd = open(fbdev_name, O_RDONLY)) == -1)
        posixError("could not open %s", fbdev_name);
    if (flag_output) {
        fprintf(stderr,"Output file: %s\n", output_file_name);
        if ((fd_ouput_file = open(output_file_name, O_WRONLY|O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
            posixError("could not open %s", output_file_name);
        if((ouput_file = fdopen(fd_ouput_file, "wb"))==NULL)
            posixError("could not open %s", output_file_name);
    }

    // Color mode checks. Default: Colored
    if (flag_gray) {
        fprintf(stderr,"Grayscale mode is selected\n");
    } else{
        // default
        flag_colored = 1;
        fprintf(stderr,"Colored mode is selected\n");
    }
    // Renk seçeneklerinin birleştirilmemesi kontrolü
    if ((flag_gray + flag_colored) > 1) {
        fprintf(stderr, "Don't mix color options!\n");
        exit(EXIT_FAILURE);
    }

    // The remains threated as mistake.
    if (optind < argc) {
        fprintf(stderr,"Non-option arguments: ");
        while (optind < argc)
            fprintf(stderr,"%s ", argv[optind++]);
        fprintf(stderr,"\n");
        exit(EXIT_FAILURE);
    }
    /// other checks
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix_info))
        posixError("FBIOGET_FSCREENINFO failed");

    if (fix_info.type != FB_TYPE_PACKED_PIXELS)
        notSupported("framebuffer type is not PACKED_PIXELS");

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var_info))
        posixError("FBIOGET_VSCREENINFO failed");

    if (var_info.red.length > 8 || var_info.green.length > 8 ||
        var_info.blue.length > 8)
        notSupported("color depth > 8 bits per component");

    // initColormap
    switch (fix_info.visual) {
    case FB_VISUAL_TRUECOLOR: {
        /* initialize dummy colormap */
        uint32_t i;
        for (i = 0; i < (1U << var_info.red.length); i++)
            colormap.red[i] = i * 0xFFFF / ((1 << var_info.red.length) - 1);
        for (i = 0; i < (1U << var_info.green.length); i++)
            colormap.green[i] = i * 0xFFFF / ((1 << var_info.green.length) - 1);
        for (i = 0; i < (1U << var_info.blue.length); i++)
            colormap.blue[i] = i * 0xFFFF / ((1 << var_info.blue.length) - 1);
        break;
    }
    case FB_VISUAL_DIRECTCOLOR:
    case FB_VISUAL_PSEUDOCOLOR:
    case FB_VISUAL_STATIC_PSEUDOCOLOR:
        if (ioctl(fd, FBIOGETCMAP, &colormap) != 0)
            posixError("FBIOGETCMAP failed");
        break;
    case FB_VISUAL_MONO01:
        is_mono = true;
        break;
    case FB_VISUAL_MONO10:
        is_mono = true;
        black_is_zero = true;
        break;
    default:
        notSupported("unsupported visual");
    }

    if (var_info.bits_per_pixel < 8 && !is_mono)
        notSupported("< 8 bpp");
    if (var_info.bits_per_pixel != 1 && is_mono)
        notSupported("monochrome framebuffer is not 1 bpp");

    // process
    /// try memory-map else use malloc
    const size_t mapped_length =
        fix_info.line_length * (var_info.yres + var_info.yoffset);
    uint8_t *video_memory =
        (uint8_t *)mmap(NULL, mapped_length, PROT_READ, MAP_SHARED, fd, 0);
    if (video_memory != MAP_FAILED)
        mmapped_memory = true;
    else {
        mmapped_memory = false;
        const size_t buffer_size = fix_info.line_length * var_info.yres;
        video_memory = (uint8_t *)malloc(buffer_size);
        if (video_memory == NULL)
            posixError("malloc failed");
        off_t offset = lseek(fd, fix_info.line_length * var_info.yoffset, SEEK_SET);
        if (offset == (off_t)-1)
            posixError("lseek failed");
        var_info.yoffset = 0;
        ssize_t read_bytes = read(fd, video_memory, buffer_size);
        if (read_bytes < 0)
            posixError("read failed");
        else if ((size_t)read_bytes != buffer_size) {
            errno = EIO;
            posixError("read failed");
        }
    }

    fflush(ouput_file);
    if (is_mono)
        dumpVideoMemoryPbm(video_memory, &var_info, black_is_zero,
                              fix_info.line_length, ouput_file);
    else if(flag_colored)
        dumpVideoMemoryPpm(video_memory, &var_info, &colormap, fix_info.line_length, ouput_file);
    else if(flag_gray)
        dumpVideoMemoryPgm(video_memory, &var_info, &colormap,
                             fix_info.line_length, ouput_file);


    // close and free
    if (fclose(stdout))
        posixError("write error");

    // deliberately ignore errors
    if (mmapped_memory)
        munmap(video_memory, mapped_length);
    else
        free(video_memory);
    close(fd);
    close(fd_ouput_file);
    fclose(ouput_file);

    return 0;
}

