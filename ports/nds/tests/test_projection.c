#include "dok/projection.h"

#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

_Static_assert(sizeof(dok_projection_camera) == 32, "camera scalar widths");
_Static_assert(sizeof(dok_projection_vertex) == 12, "vertex scalar widths");
_Static_assert(sizeof(dok_projection_sample) == 16, "native sample scalar widths");
_Static_assert(sizeof(dok_projection_q16) == 8, "target sample scalar widths");

static unsigned checks;

#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "projection check failed: %s:%d: %s\n", \
            __FILE__, __LINE__, #condition); return 1; } } while (0)

static int fixed_checks(void)
{
    dok_projection_camera camera = {15120, -300, 22800, 32768, 0, 200, 8, 4096, 159, 79};
    const dok_projection_vertex points[] = {
        {11760, -640, 22560}, {11760, -640, 23040},
        {11760, 0, 23040}, {11760, 0, 22560},
        {11760, -640, 22080}, {11760, 0, 22080}
    };
    const int32_t expected[][2] = {{145,59}, {172,59}, {172,95},
                                  {145,95}, {118,59}, {118,95}};
    dok_projection_sample native = {11,22,33,44};
    dok_projection_q16 mapped = {55,66};
    size_t i;
    /* These values occur in full unique render records from original RU RAM. */
    for (i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
        CHECK(dok_projection_native(&camera, &points[i], &native) == DOK_PROJECTION_OK);
        CHECK(native.x == expected[i][0] && native.y == expected[i][1]);
        CHECK(native.depth == 3360 && native.factor_q15 == 1840);
    }
    CHECK(dok_projection_native(NULL, &points[0], &native) == DOK_PROJECTION_INVALID_ARGUMENT);
    CHECK(native.x == 118 && native.y == 95 && native.depth == 3360);
    CHECK(dok_projection_native(&camera, NULL, &native) == DOK_PROJECTION_INVALID_ARGUMENT);
    CHECK(dok_projection_native(&camera, &points[0], NULL) == DOK_PROJECTION_INVALID_ARGUMENT);
    camera.distance = 0;
    CHECK(dok_projection_native(&camera, &(dok_projection_vertex){15120,0,22800}, &native) == DOK_PROJECTION_DIVIDE_FAULT);
    CHECK(native.x == 118 && native.factor_q15 == 1840);
    camera = (dok_projection_camera){0,0,0,0,32768,32768,-32768,2,159,79};
    CHECK(dok_projection_native(&camera, &(dok_projection_vertex){0,0,-32769}, &native) == DOK_PROJECTION_DIVIDE_FAULT);
    CHECK(native.x == 118 && native.y == 95 && native.depth == 3360 && native.factor_q15 == 1840);
    CHECK(dok_projection_map(DOK_PROJECTION_SCENE, 159, 79, &mapped) == DOK_PROJECTION_OK);
    CHECK(mapped.x == 8329626 && mapped.y == 4981965);
    CHECK(dok_projection_map(DOK_PROJECTION_SCENE, 145, 59, &mapped) == DOK_PROJECTION_OK);
    CHECK(mapped.x == 7595622 && mapped.y == 3720397);
    CHECK(dok_projection_map(DOK_PROJECTION_HUD, 0, 160, &mapped) == DOK_PROJECTION_OK);
    CHECK(mapped.x == -6554 && mapped.y == 10090906);
    CHECK(dok_projection_map(DOK_PROJECTION_STATIC, 319, 199, &mapped) == DOK_PROJECTION_OK);
    CHECK(mapped.x == 16718234 && mapped.y == 12518687);
    CHECK(dok_projection_map(DOK_PROJECTION_SCENE, INT32_MAX, INT32_MIN, &mapped) == DOK_PROJECTION_RANGE);
    CHECK(mapped.x == 16718234 && mapped.y == 12518687);
    CHECK(dok_projection_map((dok_projection_band)99, 0, 0, &mapped) == DOK_PROJECTION_INVALID_ARGUMENT);
    CHECK(mapped.x == 16718234 && mapped.y == 12518687);
    CHECK(dok_projection_map(DOK_PROJECTION_SCENE, 0, 0, NULL) == DOK_PROJECTION_INVALID_ARGUMENT);
    return 0;
}

static int independent_vectors(const char *path)
{
    FILE *file = fopen(path, "r");
    int32_t v[18];
    unsigned count = 0;
    CHECK(file != NULL);
    for (;;) {
        int n = fscanf(file,
            "%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32
            " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32
            " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32,
            &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],&v[6],&v[7],&v[8],&v[9],
            &v[10],&v[11],&v[12],&v[13],&v[14],&v[15],&v[16],&v[17]);
        dok_projection_camera camera;
        dok_projection_vertex vertex;
        dok_projection_sample out = {71,72,73,74};
        dok_projection_result result;
        if (n == EOF) {
            break;
        }
        CHECK(n == 18);
        camera = (dok_projection_camera){v[0],v[1],v[2],v[3],v[4],v[5],
                                      (int16_t)v[6],(int16_t)v[7],(int16_t)v[8],(int16_t)v[9]};
        vertex = (dok_projection_vertex){v[10],v[11],v[12]};
        result = dok_projection_native(&camera, &vertex, &out);
        CHECK((int32_t)result == v[13]);
        CHECK(out.x == v[14] && out.y == v[15] && out.depth == v[16] && out.factor_q15 == v[17]);
        ++count;
    }
    CHECK(fclose(file) == 0);
    CHECK(count >= 5000);
    return 0;
}

static int independent_maps(const char *path)
{
    FILE *file = fopen(path, "r");
    int32_t v[6];
    unsigned count = 0;
    CHECK(file != NULL);
    for (;;) {
        int n = fscanf(file, "%" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32 " %" SCNd32,
                       &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
        dok_projection_q16 out = {81,82};
        dok_projection_result result;
        if (n == EOF) {
            break;
        }
        CHECK(n == 6);
        result = dok_projection_map((dok_projection_band)v[0], v[1], v[2], &out);
        CHECK((int32_t)result == v[3]);
        CHECK(out.x == v[4] && out.y == v[5]);
        ++count;
    }
    CHECK(fclose(file) == 0);
    CHECK(count >= 1500);
    return 0;
}

int main(int argc, char **argv)
{
    if (fixed_checks() != 0) {
        return EXIT_FAILURE;
    }
    if (argc > 1 && independent_vectors(argv[1]) != 0) {
        return EXIT_FAILURE;
    }
    if (argc > 2 && independent_maps(argv[2]) != 0) {
        return EXIT_FAILURE;
    }
    printf("N06 projection PASS: %u checks%s\n", checks,
           argc > 1 ? " (with independent ASM-derived vectors)" : " (fixed evidence only)");
    return EXIT_SUCCESS;
}
