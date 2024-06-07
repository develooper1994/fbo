#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>

#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <endian.h>
#include <pthread.h>

#include <linux/fb.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <arm_neon.h>

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
#define VERSION_MINOR "0.6"
#define VERSION VERSION_MAJOR "." VERSION_MINOR
#define INTRO "This software captures what printed to framebuffer. \n" \
    "Software only supports pbm(P4), pgm(P5) and ppm(P6) image formats. \n" \
    "Special thanks to https://github.com/jwilk/fbcat repo!"
#define DefaultFbDev "/dev/fb"
#define DefaultOutputFile stdout
#define Author "* Author: Mustafa Selçuk Çağlar\n"
#define BugTrackerUrl "https://github.com/develooper1994/fbo/issues"
#define HELPTEXT \
"\n" \
INTRO "\n" \
"VERSION: " VERSION "\n" \
"-h <noarg> or --help <noarg> : print help \n" \
"-v <noarg> or --version <noarg> : print the version \n" \
"-d or --device <arg> : framebuffer device. Default: " DefaultFbDev "\n" \
"-o or --output <arg> : output file \n" \
"-g or --gray <noarg> : grayscale color mode. P5, pgm file format\n" \
"-c or --colored <noarg> : full color mode. P6, ppm file format\n" \
"-b or --colored <noarg> : bitmap file format otherwise file format is pgm or ppm\n>"\
"-t or --thread <noarg> : Use all cores of the processor. It may affect on multicore systems on bigger screens. (only PGM and PPM for now)\n" \
"Don't mix color options! \n"

// file types
#define PBM "pbm"
#define PGM "pgm"
#define PPM "ppm"
#define BMP "bmp"

// exit codes
#define EXIT_POSIX_ERROR 2
#define EXIT_NOT_SUPPORTED 3
#define EXIT_HELP 4

typedef struct fb_fix_screeninfo fsi;
typedef struct fb_var_screeninfo vsi;
typedef struct fb_cmap cmap;
static bool black_is_zero = false;

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
// fill the blanks as you wish
/// BMP file header
static BITMAPFILEHEADER file_header = {
    .bfType = 0x4D42,  // 'BM'
    // .bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size,
    .bfReserved1 = 0,
    .bfReserved2 = 0,
    .bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
};
/// BMP info header
static BITMAPINFOHEADER info_header = {
    .biSize = sizeof(BITMAPINFOHEADER),
    // .biWidth = width,
    // .biHeight = -height, // top-down BMP
    .biPlanes = 1,
    // .biBitCount
    .biCompression = 0,
    // .biSizeImage = data_size,
    .biXPelsPerMeter = 0,
    .biYPelsPerMeter = 0,
    // info_header.biClrUsed = 0 // all colors are important
    // info_header.biClrImportant = 0; // all colors are important
};

// utility functions
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
    printf(HELPTEXT);
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
typedef struct {
    const uint8_t *video_memory;
    const vsi *info;
    const cmap *colormap;
    uint32_t line_length;
    uint8_t *buffer;
    uint32_t start_row;
    uint32_t num_rows;
    //BMP
    uint16_t bit_count;
} ThreadData;
typedef struct ThreadNode {
    pthread_t thread;
    ThreadData data;
    struct ThreadNode *next;
} ThreadNode;

// PBM, PGM, PPM
void* processPbmRows(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    const uint32_t bytes_per_row = (data->info->xres + 7) / 8;
    const uint32_t row_step = bytes_per_row;
    uint8_t *row = data->buffer + data->start_row * row_step;

    if (data->info->xoffset % 8) {
        notSupported("xoffset not divisible by 8 in 1 bpp mode");
    }

    for (uint32_t y = data->start_row; y < data->start_row + data->num_rows; y++) {
        const uint8_t *current = data->video_memory + (y + data->info->yoffset) * data->line_length +
                                 data->info->xoffset / 8;
        for (uint32_t x = 0; x < bytes_per_row; x++) {
            row[x] = reverseBits(*current++);
            //if (data->colormap) { // If colormap is used as black_is_zero flag
            if (black_is_zero) { // If colormap is used as black_is_zero flag
                row[x] = ~row[x];
            }
        }
        row += row_step;
    }
    return NULL;
}
void* processPgmRows(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    const uint32_t bytes_per_pixel = (data->info->bits_per_pixel + 7) / 8;
    const uint32_t row_step = data->info->xres;
    uint8_t *row = data->buffer + data->start_row * row_step;

    for (uint32_t y = data->start_row; y < data->start_row + data->num_rows; y++) {
        const uint8_t *current = data->video_memory + (y + data->info->yoffset) * data->line_length +
                                 data->info->xoffset * bytes_per_pixel;
        for (uint32_t x = 0; x < data->info->xres; x++) {
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
            row[x] = getGrayscale(pixel, data->info, data->colormap);
        }
        row += row_step;
    }
    return NULL;
}
void* processPpmRows(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    const uint32_t bytes_per_pixel = (data->info->bits_per_pixel + 7) / 8;
    uint32_t row_step = data->info->xres * 3;
    uint8_t *row = data->buffer + data->start_row * row_step;

    for (uint32_t y = data->start_row; y < data->start_row + data->num_rows; ++y) {
        const uint8_t *current = data->video_memory + (y + data->info->yoffset) * data->line_length +
                                 data->info->xoffset * bytes_per_pixel;
        for (uint32_t x = 0; x < data->info->xres; ++x) {
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
                for (uint32_t i = 0; i < bytes_per_pixel; ++i) {
                    pixel |= *current << (i * sizeof(typeof(row)));
                    current++;
                }
                break;
            }

            /*
            row[x * 3 + 0] = getColor(pixel, &data->info->red, data->colormap->red);
            row[x * 3 + 1] = getColor(pixel, &data->info->green, data->colormap->green);
            row[x * 3 + 2] = getColor(pixel, &data->info->blue, data->colormap->blue);
            // row[x * 3 + 3] = getColor(pixel, &data->info->transp, data->colormap->transp);
            */

            // Initialize RgbaPixel
            // row[x * 3] = pixel; // red <-> blue colors changed
            // row[x * 3] = (getColor(...; // memory alignment problem with "uint32_t* row"!
            pixel =
                (getColor(pixel, &data->info->red, data->colormap->red) << 0) |
                (getColor(pixel, &data->info->green, data->colormap->green) << 8) |
                (getColor(pixel, &data->info->blue, data->colormap->blue) << 16);
                // (getColor(pixel, &data->info->transp, data->colormap->transp) << 24);
            memmove(&row[x * 3], &pixel, sizeof(pixel));
        }
        row += row_step; // /(sizeof(typeof(row))/sizeof(uint8_t));
    }
    return NULL;
}
void* processBmpGrayscaleRows(void *arg){
    ThreadData *data = (ThreadData *)arg;

    return NULL;
}
void* processBmpColoredRows(void *arg){
    ThreadData *data = (ThreadData *)arg;

    return NULL;
}

static inline void dumpVideoMemory(const uint8_t *video_memory, const vsi *info, const struct fb_cmap *colormap, uint32_t line_length, FILE *fp, int use_multithreading, const char *format) {
    //const uint32_t bytes_per_pixel = (info->bits_per_pixel + 7) / 8;
    const uint32_t row_step = //(format[1] == '6') ? info->xres * 3 :
                        (format[1] == '6') ? info->xres * 3 :
                        (format[1] == '5') ? info->xres :
                        (format[1] == '4') ? (info->xres + 7) / 8 : 0;
    const uint32_t image_size = info->yres * row_step;
    uint8_t *buffer = (uint8_t *)malloc(image_size);
    if (buffer == NULL) {
        posixError("malloc failed");
    }

    ThreadData data = {
        .video_memory = video_memory,
        .info = info,
        .colormap = colormap,
        .line_length = line_length,
        .buffer = buffer,
        // .start_row = 0,
        // .num_rows = info->yres
    };

    fprintf(fp, "%s %" PRIu32 " %" PRIu32 " 255\n", format, info->xres, info->yres);

    if (use_multithreading) {
        // Get the number of available processors at runtime
        uint32_t num_threads = sysconf(_SC_NPROCESSORS_ONLN);
        num_threads = num_threads > 0 ? num_threads : 1;

        // Initialize the linked list for threads
        ThreadNode *head = NULL, *tail = NULL;
        const uint32_t rows_per_thread = info->yres / num_threads;
        const uint32_t remaining_rows = info->yres % num_threads;

        for (uint32_t i = 0; i < num_threads; ++i) {
            ThreadNode *node = (ThreadNode *)malloc(sizeof(ThreadNode));
            if (!node) {
                posixError("malloc failed for ThreadNode");
            }

            data.num_rows = rows_per_thread;
            data.start_row = i * rows_per_thread;
            node->data = data;

            if (i == num_threads - 1) {
                node->data.num_rows += remaining_rows; // Add remaining rows to the last thread
            }
            node->next = NULL;

            if (tail) {
                tail->next = node;
            } else {
                head = node;
            }
            tail = node;

            if (format[1] == '6') {
                pthread_create(&node->thread, NULL, processPpmRows, &node->data);
            } else if (format[1] == '5') {
                pthread_create(&node->thread, NULL, processPgmRows, &node->data);
            } else if (format[1] == '4') {
                pthread_create(&node->thread, NULL, processPbmRows, &node->data);
            }
        }

        // Join all threads
        ThreadNode *current = head;
        while (current) {
            pthread_join(current->thread, NULL);
            ThreadNode *next = current->next;
            free(current);
            current = next;
        }
    } else {
        // Prepare thread data for the entire image

        data.num_rows = info->yres;
        data.start_row = 0;

        // Process all rows serially
        if (format[1] == '6') {
            processPpmRows(&data);
        } else if (format[1] == '5') {
            processPgmRows(&data);
        } else if (format[1] == '4') {
            processPbmRows(&data);
        }
    }

    if (fwrite(buffer, image_size, 1, fp) != 1) {
        posixError("write error");
    }

    free(buffer);
}
// BMP
static inline void writeBmpHeader(uint32_t image_size, uint32_t width, uint32_t height, uint16_t bit_count, FILE *fp) {
    //BITMAPFILEHEADER file_header = {0x4D42, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size, 0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
    //BITMAPINFOHEADER info_header = {sizeof(BITMAPINFOHEADER), width, -height, 1, bit_count, 0, image_size, 0, 0, (bit_count == 8) ? 256 : 0, (bit_count == 8) ? 256 : 0};

    // BMP file header
    file_header.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size;

    // BMP info header
    info_header.biWidth = width;
    info_header.biHeight = -height; // top-down BMP
    info_header.biBitCount = bit_count;
    info_header.biSizeImage = image_size;
    info_header.biClrUsed = info_header.biClrImportant =
        (bit_count == 8) ? 256 : 0; // only 256 color range important : all colors are important

    if(fwrite(&file_header, sizeof(file_header), 1, fp) != 1){
        posixError("write error");
    }
    if(fwrite(&info_header, sizeof(info_header), 1, fp) != 1){
        posixError("write error");
    }

    if (bit_count == 8) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint8_t color[4] = {i, i, i, 0};
            if(fwrite(color, sizeof(color), 1, fp) != 1){
                posixError("write error");
            }
        }
    }
}
static inline void dumpVideoMemoryBmpGrayscale(
  const uint8_t *video_memory,
  const vsi *info,
  const cmap *colormap,
  const uint32_t line_length,
  FILE *fp
) {
  const uint32_t bytes_per_pixel = (info->bits_per_pixel + 7) / 8;
  const uint32_t width = info->xres;
  const uint32_t height = info->yres;
  const uint32_t bit_count = 8;
  const uint32_t row_step = (info->xres + 3) & (~3);
  const uint32_t image_size = row_step * info->yres;
  uint8_t * row = (uint8_t *)malloc(image_size);
  if (row == NULL){
    posixError("malloc failed");
  }
  assert(row != NULL);
  uint8_t * const  buffer_start_addr = row;
  // uint8_t *buffer_start_point = (uint8_t *)malloc(row_step);

  /*
  // BMP file header
  file_header.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size;

  // BMP info header
  info_header.biWidth = width;
  info_header.biHeight = -height; // top-down BMP
  info_header.biBitCount = bit_count;
  info_header.biSizeImage = image_size;
  info_header.biClrUsed = info_header.biClrImportant = 256; // only 256 color range important

  fwrite(&file_header, sizeof(file_header), 1, fp);
  fwrite(&info_header, sizeof(info_header), 1, fp);

  // Write grayscale palette
  for (uint32_t i = 0; i < 256; ++i) {
    uint8_t color[4] = {i, i, i, 0};
    fwrite(color, sizeof(color), 1, fp);
  }
  */

  writeBmpHeader(image_size, width, height, bit_count, fp);

  // processBmpGrayscaleRows
  for (uint32_t y = 0; y < info->yres; ++y) {
    const uint8_t *current = video_memory + (y + info->yoffset) * line_length +
                             info->xoffset * bytes_per_pixel;
    // horizontal scan
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
          for (uint32_t i = 0; i < bytes_per_pixel; ++i) {
            pixel |= *current << (i * sizeof(typeof(row)));
            ++current;
          }
          break;
      }
      row[x] = getGrayscale(pixel, info, colormap);
    }
    row += row_step;
  }

  if (fwrite(buffer_start_addr, image_size, 1, fp) != 1){
    posixError("write error");
  }

  free(buffer_start_addr);
}

static inline void dumpVideoMemoryBmpColored(
  const uint8_t *video_memory,
  const vsi *info,
  const cmap *colormap,
  const uint32_t line_length,
  FILE *fp
) {
  (void)colormap;
  const uint32_t bytes_per_pixel = (info->bits_per_pixel + 7) / 8;
  const uint32_t width = info->xres;
  const uint32_t height = info->yres;
  const uint32_t bit_count = 24;
  const uint32_t row_step = (width * 3 + 3) & (~3); // 3 bytes per pixel (RGB)
  const uint32_t image_size = row_step * height;
  uint8_t * row = (uint8_t *)malloc(image_size);
  if (row == NULL){
    posixError("malloc failed");
  }
  assert(row != NULL);
  uint8_t *buffer_start_addr = row;
  //uint8_t *buffer_start_point = (uint8_t *)malloc(row_step);

  /*
  // BMP header header
  file_header.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + image_size;

  // BMP info header
  info_header.biWidth = width;
  info_header.biHeight = -height; // top-down BMP
  info_header.biBitCount = 24;
  info_header.biSizeImage = image_size;
  info_header.biClrUsed = info_header.biClrImportant = 0; // all colors are important

  fwrite(&file_header, sizeof(file_header), 1, fp);
  fwrite(&info_header, sizeof(info_header), 1, fp);
  */

  writeBmpHeader(image_size, width, height, bit_count, fp);

  // processBmpRows
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t *current = video_memory + (y + info->yoffset) * line_length + info->xoffset * bytes_per_pixel;
    for (uint32_t x = 0; x < width; ++x) {
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
          for (unsigned int i = 0; i < bytes_per_pixel; ++i) {
            pixel |= *current << (i * 8);
            ++current;
          }
          break;
      }

        /*
        row[x * 3 + 0] = getColor(pixel, &info->blue, colormap->blue);
        row[x * 3 + 1] = getColor(pixel, &info->green, colormap->green);
        row[x * 3 + 2] = getColor(pixel, &info->red, colormap->red);
        // row[x * 3 + 3] = getColor(pixel, &info->transp, colormap->transp);
        */

      /*
        rgbPixel.components =
          (getColor(pixel, &info->red, colormap->red) << 16) |
          (getColor(pixel, &info->green, colormap->green) << 8) |
          (getColor(pixel, &info->blue, colormap->blue) << 0);
          // (getColor(pixel, &data->info->transp, data->colormap->transp) << 24);
        memmove(&row[x * 3], &rgbPixel.components, sizeof(rgbPixel.components));
      */
      memmove(&row[x * 3], &pixel, sizeof(pixel));
      // row[x * 3] = pixel;
    }
    row += row_step;
  }

  if (fwrite(buffer_start_addr, image_size, 1, fp) != 1){
    posixError("write error");
  }

  free(buffer_start_addr);
}



int main(int argc, char **argv){
    // init
    char *fbdev_name = DefaultFbDev;
    int fd, fd_ouput_file;
    FILE *ouput_file = DefaultOutputFile;
    bool mmapped_memory = false, is_mono = false;
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
    int flag_help = 0, flag_version = 0, flag_device = 0, flag_output = 0,
        flag_gray = 0, flag_colored = 0, flag_bitmap = 0,
        flag_thread = 0,
        flag_err = 0;
    char *output_file_name;

    /*
    if (isatty(STDOUT_FILENO)) {
        fprintf(stderr, "fbo: refusing to write binary data to a terminal\n");
        flag_err = 1;
    }
    */

    // Kısa ve Uzun seçenekleri tanımlama
    static const char* short_options = "hvd:o:gcbt";
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {"device", optional_argument, 0, 'd'},
        {"output", optional_argument, 0, 'o'},
        {"gray", no_argument, 0, 'g'},
        {"colored", no_argument, 0, 'c'},
        {"bitmap", no_argument, 0, 'b'},
        {"thread", no_argument, 0, 't'},
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
        case 'b':
            flag_bitmap = 1;
            break;
        case 't':
            flag_thread = 1;
            break;
        case '?':
            // error part
            if (optopt == 'd'){
                fprintf(stderr, "option -d or --device without argument!. Device " DefaultFbDev "\n");
            } else if (optopt == 'o'){
                fprintf(stderr, "option -o or --output without argument!...\n");
            } else if (optopt != 0) {
                fprintf(stderr, "invalid option: -%c\n", optopt);
            } else {
                fprintf(stderr, "invalid long option!...\n");
                //fprintf(stderr, "invalid long option: %s\n", argv[optind - 1]);
            }
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
            fbdev_name = DefaultFbDev;
        fprintf(stderr,"Framebuffer device: %s\n", fbdev_name);
    }
    if ((fd = open(fbdev_name, O_RDONLY)) == -1){
        posixError("could not open %s", fbdev_name);
    }
    if (flag_output) {
        fprintf(stderr,"Output file: %s\n", output_file_name);
        if ((fd_ouput_file = open(output_file_name, O_WRONLY|O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) == -1)
            posixError("could not open %s", output_file_name);
        if((ouput_file = fdopen(fd_ouput_file, "wb"))==NULL)
            posixError("could not open %s", output_file_name);
    }

    // Color mode checks. Default: Colored
    if (flag_gray) {
        fprintf(stderr,"Grayscale color mode is selected\n");
    } else{
        // default
        flag_colored = 1;
        fprintf(stderr,"Full color mode is selected\n");
    }
    // Renk seçeneklerinin birleştirilmemesi kontrolü
    if ((flag_gray + flag_colored) > 1) {
        fprintf(stderr, "Don't mix color modes!\n");
        exit(EXIT_FAILURE);
    }
    if(flag_bitmap){
        fprintf(stderr,"Bitmap file mode mode is selected\n");
    }
    if(flag_thread){
        fprintf(stderr,"Thread run mode mode is selected\n");
        /*
        if(flag_gray || !flag_colored){
            fprintf(stderr, "thread run mode only supported only with P6, PPM file format for now!\n");
            exit(EXIT_FAILURE);
        }
        */
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
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix_info)){
        posixError("FBIOGET_FSCREENINFO failed");
    }

    if (fix_info.type != FB_TYPE_PACKED_PIXELS){
        notSupported("framebuffer type is not PACKED_PIXELS");
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &var_info)){
        posixError("FBIOGET_VSCREENINFO failed");
    }

    if (var_info.red.length > 8 || var_info.green.length > 8 ||
        var_info.blue.length > 8){
        notSupported("color depth > 8 bits per component");
    }

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
        if (ioctl(fd, FBIOGETCMAP, &colormap) != 0){
            posixError("FBIOGETCMAP failed");
        }
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

    if (var_info.bits_per_pixel < 8 && !is_mono){
        notSupported("< 8 bpp");
    }
    if (var_info.bits_per_pixel != 1 && is_mono){
        notSupported("monochrome framebuffer is not 1 bpp");
    }

    // process
    /// try memory-map else use malloc
    const size_t mapped_length = fix_info.line_length * (var_info.yres + var_info.yoffset);
    uint8_t *video_memory = (uint8_t *)mmap(NULL, mapped_length, PROT_READ, MAP_SHARED, fd, 0);
    if (video_memory != MAP_FAILED){
        mmapped_memory = true;
    } else {
        mmapped_memory = false;
        const size_t buffer_size = fix_info.line_length * var_info.yres;
        video_memory = (uint8_t *)malloc(buffer_size);
        if (video_memory == NULL){
            posixError("malloc failed");
        }
        off_t offset = lseek(fd, fix_info.line_length * var_info.yoffset, SEEK_SET);
        if (offset == (off_t)-1){
            posixError("lseek failed");
        }
        var_info.yoffset = 0;
        ssize_t read_bytes = read(fd, video_memory, buffer_size);
        if (read_bytes < 0){
            posixError("read failed");
        } else if ((size_t)read_bytes != buffer_size) {
            errno = EIO;
            posixError("read failed");
        }
    }

    fflush(ouput_file);
    if(flag_bitmap){
        if(flag_colored){
            dumpVideoMemoryBmpColored(video_memory, &var_info, &colormap, fix_info.line_length, ouput_file);
        } else if(flag_gray){
            dumpVideoMemoryBmpGrayscale(video_memory, &var_info, &colormap, fix_info.line_length, ouput_file);
        }
    } else{
        //dumpVideoMemoryPpm(video_memory, &var_info, &colormap, fix_info.line_length, ouput_file, flag_thread);
        dumpVideoMemory(video_memory, &var_info, &colormap, fix_info.line_length, ouput_file, flag_thread,
            is_mono ? "P4" :
            flag_colored ? "P6" : "P5");
    }


    // close and free
    if (fclose(stdout)){
        posixError("write error");
    }

    // deliberately ignore errors
    (void)(mmapped_memory ? munmap(video_memory, mapped_length) : free(video_memory));

    close(fd);
    close(fd_ouput_file);
    fclose(ouput_file);

    return 0;
}
