// Headless playtest driver: runs the ROM in mGBA, feeds button presses from a script
// file and saves screenshots. Build with `make -C tools/daily/playtest` (needs libmgba-dev).
//
// Script commands, one per line (# starts a comment):
//   wait N          run N frames with no buttons held
//   press KEY [N]   hold KEY for N frames (default 6), then release for 12 frames
//   repeat N KEY    press KEY N times
//   poke8 ADDR V    write byte V to ADDR
//   shot NAME       save the screen to NAME.png in the output directory
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct mCore *core;
static color_t *video;
static unsigned width, height;
static const char *outDir;
static uint32_t keys;

static void Log(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
    (void)logger; (void)category; (void)level; (void)format; (void)args;
}

static void RunFrames(int n)
{
    core->setKeys(core, keys);
    for (int i = 0; i < n; i++)
        core->runFrame(core);
}

static int KeyBit(const char *name)
{
    static const char *const names[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
    for (int i = 0; i < 10; i++)
        if (strcmp(name, names[i]) == 0)
            return i;
    fprintf(stderr, "unknown key %s\n", name);
    exit(1);
}

static void Press(const char *key, int frames)
{
    keys = 1u << KeyBit(key);
    RunFrames(frames);
    keys = 0;
    RunFrames(12);
}

static void Shot(const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", outDir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    png_init_io(png, f);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);
    unsigned char *row = malloc(width * 3);
    for (unsigned y = 0; y < height; y++)
    {
        for (unsigned x = 0; x < width; x++)
        {
            uint32_t c = video[y * width + x];
            row[x * 3 + 0] = c & 0xFF;
            row[x * 3 + 1] = (c >> 8) & 0xFF;
            row[x * 3 + 2] = (c >> 16) & 0xFF;
        }
        png_write_row(png, row);
    }
    png_write_end(png, NULL);
    png_destroy_write_struct(&png, &info);
    free(row);
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s ROM SCRIPT OUTDIR [SAVE]\n", argv[0]);
        return 1;
    }
    static struct mLogger logger = { .log = Log };
    mLogSetDefaultLogger(&logger);

    outDir = argv[3];
    core = mCoreFind(argv[1]);
    if (!core || !core->init(core))
        return 1;
    mCoreInitConfig(core, NULL);
    core->desiredVideoDimensions(core, &width, &height);
    video = calloc(width * height, sizeof(color_t));
    core->setVideoBuffer(core, video, width);
    if (!mCoreLoadFile(core, argv[1]))
        return 1;
    if (argc > 4)
    {
        struct VFile *save = VFileOpen(argv[4], O_CREAT | O_RDWR);
        core->loadSave(core, save);
    }
    core->reset(core);

    FILE *script = fopen(argv[2], "r");
    if (!script) { perror(argv[2]); return 1; }
    char line[256];
    while (fgets(line, sizeof(line), script))
    {
        char cmd[32], arg1[128], arg2[128];
        int n = sscanf(line, "%31s %127s %127s", cmd, arg1, arg2);
        if (n <= 0 || cmd[0] == '#')
            continue;
        if (!strcmp(cmd, "wait"))
            RunFrames(atoi(arg1));
        else if (!strcmp(cmd, "press"))
            Press(arg1, n > 2 ? atoi(arg2) : 6);
        else if (!strcmp(cmd, "repeat"))
            for (int i = atoi(arg1); i > 0; i--)
                Press(arg2, 6);
        else if (!strcmp(cmd, "shot"))
            Shot(arg1);
        else if (!strcmp(cmd, "poke8"))
            core->busWrite8(core, strtoul(arg1, NULL, 0), strtoul(arg2, NULL, 0));
        else
        {
            fprintf(stderr, "unknown command %s\n", cmd);
            return 1;
        }
    }
    core->deinit(core);
    return 0;
}
