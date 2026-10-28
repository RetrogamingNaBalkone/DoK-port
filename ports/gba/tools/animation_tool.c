#include "dok/animation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t fnv(const uint8_t *p, size_t size) {
    uint32_t h = UINT32_C(2166136261);
    size_t i;
    for (i = 0; i < size; ++i) h = (h ^ p[i]) * UINT32_C(16777619);
    return h;
}
int main(int argc, char **argv) {
    FILE *file;
    long length;
    uint8_t *bytes;
    int export_frames;
    dok_animation_view animation;
    dok_animation_iterator it;
    dok_animation_frame frame;
    dok_asset_status status;
    if (!((argc == 3 && (!strcmp(argv[1], "--list") || !strcmp(argv[1], "--image"))) ||
          (argc == 4 && !strcmp(argv[1], "--export")))) {
        fprintf(stderr, "Usage: dok-anim --list FILE | --image FILE | --export FILE EXISTING_DIRECTORY\n"); return 2;
    }
    export_frames = argc == 4;
    file = fopen(argv[2], "rb");
    if (!file) { perror(argv[2]); return 2; }
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET)) { fclose(file); return 2; }
    bytes = (uint8_t *)malloc(length ? (size_t)length : 1u);
    if (!bytes) { fclose(file); return 2; }
    if (fread(bytes, 1, (size_t)length, file) != (size_t)length || ferror(file)) { fclose(file); free(bytes); return 2; }
    fclose(file);
    if (!strcmp(argv[1], "--image")) {
        dok_image_view image;
        status = dok_image_open(bytes, (size_t)length, &image);
        if (status != DOK_ASSET_OK) { fprintf(stderr, "%s\n", dok_asset_status_string(status)); free(bytes); return 1; }
        puts("width\theight\tflags\tpixel_bytes\tfnv1a32");
        printf("%u\t%u\t%u\t%lu\t%08lx\n", (unsigned)image.width, (unsigned)image.height,
               (unsigned)image.stored_flags, (unsigned long)image.pixel_bytes,
               (unsigned long)fnv(image.pixels, image.pixel_bytes));
        free(bytes); return 0;
    }
    status = dok_animation_open(bytes, (size_t)length, &animation);
    if (status != DOK_ASSET_OK) { fprintf(stderr, "%s\n", dok_asset_status_string(status)); free(bytes); return 1; }
    status = dok_animation_begin(&animation, &it);
    if (status != DOK_ASSET_OK) { free(bytes); return 1; }
    puts("index\tinterval_bits\tinterval_ticks\tparameter_a\tparameter_b\timage_offset\twidth\theight\tflags\tpixel_bytes\tfnv1a32");
    while ((status = dok_animation_next(&it, &frame)) == DOK_ASSET_OK) {
        printf("%u\t%u\t%d\t%d\t%d\t%llu\t%u\t%u\t%u\t%lu\t%08lx\n",
               (unsigned)frame.index, (unsigned)frame.interval_bits, (int)frame.interval_ticks,
               (int)frame.parameter_a, (int)frame.parameter_b, (unsigned long long)frame.image_offset,
               (unsigned)frame.image.width, (unsigned)frame.image.height, (unsigned)frame.image.stored_flags,
               (unsigned long)frame.image.pixel_bytes, (unsigned long)fnv(frame.image.pixels, frame.image.pixel_bytes));
        if (export_frames) {
            char path[1024];
            int n = snprintf(path, sizeof(path), "%s/frame_%04u.pgm", argv[3], (unsigned)frame.index);
            int failed;
            if (n < 0 || (size_t)n >= sizeof(path)) { free(bytes); return 2; }
            file = fopen(path, "wb");
            if (!file) { perror(path); free(bytes); return 2; }
            failed = fprintf(file, "P5\n%u %u\n255\n", (unsigned)frame.image.width, (unsigned)frame.image.height) < 0;
            if (!failed && fwrite(frame.image.pixels, 1, frame.image.pixel_bytes, file) != frame.image.pixel_bytes) failed = 1;
            if (fclose(file)) failed = 1;
            if (failed) { free(bytes); return 2; }
        }
    }
    free(bytes);
    return status == DOK_ASSET_END ? 0 : 1;
}
