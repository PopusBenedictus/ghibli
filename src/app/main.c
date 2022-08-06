#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <bass.h>
#include <bassmix.h>


#ifdef WIN32
#include <SDL2/SDL_syswm.h>
#include <direct.h>
#include <winsock.h>
#else
#include <sys/time.h>
#endif

#define GA_WIDTH (1024)
#define GA_HEIGHT (768)
#define GA_START_POS_X (100)
#define GA_START_POS_Y (100)
#define GA_TITLE "Ghibli"
#define GA_IMGPAL_MAXSZ (524288)
#define GA_FFT_SIZE (8192)
#define GA_WF_NUM_COLORS (6)

SDL_Window *mainWindow = NULL;
SDL_Surface *mainWindowSurface = NULL;
SDL_Renderer *mainRenderer = NULL;
SDL_Texture *waterfallTexture = NULL;
SDL_Surface *albumArt = NULL;
SDL_Color *currentPalette = NULL;
HSTREAM bassMixStream = 0;
HSTREAM musicStream = 0;

float ddpi,
      hdpi,
      vdpi,
      currentScrollRateTarget,
      currentWaterfallFftFactor;

float waterfallThresholds[] = {
        -100.0f,
        -60.0f,
        -30.0f,
        -20.0f,
        -10.0f,
        -5.0f,
        0.0f
};

float currentData[GA_FFT_SIZE] = {0};

struct timeval lastCall;
float frameLimit;
float lastDelta;

#ifdef WIN32
static SDL_SysWMinfo mainWindowInfo = {0};
#endif

int gA_SleepMillis(float ms) {
#ifdef WIN32
    Sleep((DWORD)roundf(ms));
    return 0;
#else
    struct timespec ts;
    int res;

    if (ms < 0.0f) {
        errno = EINVAL;
        return -1;
    }

    ts.tv_sec = (time_t)roundf(ms / 1000.0f);
    ts.tv_nsec = ((int)roundf(ms) % 1000) * 1000000;

    do {
        res = nanosleep(&ts, NULL);
    } while (res && errno == EINTR);

    return res;
#endif
}

#ifdef WIN32
LARGE_INTEGER gA_get_FILETIME_offset()
{
    SYSTEMTIME s;
    FILETIME f;
    LARGE_INTEGER t;

    s.wYear = 1970;
    s.wMonth = 1;
    s.wDay = 1;
    s.wHour = 0;
    s.wMinute = 0;
    s.wSecond = 0;
    s.wMilliseconds = 0;
    SystemTimeToFileTime(&s, &f);
    t.QuadPart = f.dwHighDateTime;
    t.QuadPart <<= 32;
    t.QuadPart |= f.dwLowDateTime;
    return (t);
}
#endif

int gA_GetTime(struct timeval *tv) {
#ifdef WIN32
    LARGE_INTEGER           t;
    FILETIME                f;
    double                  microseconds;
    static LARGE_INTEGER    offset;
    static double           frequencyToMicroseconds;
    static int              initialized = 0;
    static BOOL             usePerformanceCounter = 0;

    if (!initialized) {
        LARGE_INTEGER performanceFrequency;
        initialized = 1;
        usePerformanceCounter = QueryPerformanceFrequency(&performanceFrequency);
        if (usePerformanceCounter) {
            QueryPerformanceCounter(&offset);
            frequencyToMicroseconds = (double)performanceFrequency.QuadPart / 1000000.;
        } else {
            offset = gA_get_FILETIME_offset();
            frequencyToMicroseconds = 10.;
        }
    }
    if (usePerformanceCounter) QueryPerformanceCounter(&t);
    else {
        GetSystemTimeAsFileTime(&f);
        t.QuadPart = f.dwHighDateTime;
        t.QuadPart <<= 32;
        t.QuadPart |= f.dwLowDateTime;
    }

    t.QuadPart -= offset.QuadPart;
    microseconds = (double)t.QuadPart / frequencyToMicroseconds;
    t.QuadPart = microseconds;
    tv->tv_sec = t.QuadPart / 1000000;
    tv->tv_usec = t.QuadPart % 1000000;
    return (0);
#else
    struct timespec ts;
    int retval = clock_gettime(CLOCK_MONOTONIC, &ts);
    tv->tv_usec = ts.tv_nsec / 1000;
    tv->tv_sec = ts.tv_sec;
    return retval;
#endif
}

float gA_GetDiffTime(const struct timeval *start, const struct timeval *stop) {
    struct timeval result;
#ifdef __linux__
    timersub(stop, start, &result);
#else
    struct timeval startTmp;
    startTmp.tv_sec = start->tv_sec;
    startTmp.tv_usec = start->tv_usec;

    /* Perform the carry for the later subtraction by updating startTmp. */
    if (stop->tv_usec < startTmp.tv_usec) {
        int nsec = (startTmp.tv_usec - stop->tv_usec) / 1000000 + 1;
        startTmp.tv_usec -= 1000000 * nsec;
        startTmp.tv_sec += nsec;
    }
    if (stop->tv_usec - startTmp.tv_usec > 1000000) {
        int nsec = (stop->tv_usec - startTmp.tv_usec) / 1000000;
        startTmp.tv_usec += 1000000 * nsec;
        startTmp.tv_sec -= nsec;
    }

    result.tv_sec = stop->tv_sec - startTmp.tv_sec;
    result.tv_usec = stop->tv_usec - startTmp.tv_usec;
#endif
    return ((float)result.tv_sec * 1000.0f) + ((float)result.tv_usec / 1000.0f);
}

float gA_UseFpsManager() {
    struct timeval currentTimeCall = {0};
    float currentDelta = 0.0f;

    gA_GetTime(&currentTimeCall);

    if (lastCall.tv_usec == 0) {
        currentDelta = frameLimit;
    } else {
        currentDelta = gA_GetDiffTime(&lastCall, &currentTimeCall);
    }

    if (currentDelta < frameLimit) {
        gA_SleepMillis(frameLimit - currentDelta);
        gA_GetTime(&currentTimeCall);
        currentDelta = gA_GetDiffTime(&lastCall, &currentTimeCall);
    }

    lastCall.tv_sec = currentTimeCall.tv_sec;
    lastCall.tv_usec = currentTimeCall.tv_usec;
    lastDelta = currentDelta;
    return lastDelta;
}

float gA_GetDecibelFromFftMagnitude(float magnitude) {
    return log10f(magnitude) * 20.0f;
}

SDL_Color gA_GetColorForMagnitude(float magnitude, SDL_Color *palette) {
    float db = gA_GetDecibelFromFftMagnitude(magnitude);
    int targetIndexStart = 0;
    SDL_Color retVal;

    for (int i = 0; i < GA_WF_NUM_COLORS; ++i) {
        if (db > waterfallThresholds[i])
            continue;

        // In case magnitudes are provided resulting in readings below -100dB.
        if (i == 0) {
            retVal = palette[0];
            return retVal;
        }

        targetIndexStart = i - 1;
        break;
    }

    SDL_Color start, stop;
    memcpy(&start, &palette[targetIndexStart], sizeof(SDL_Color));
    memcpy(&stop, &palette[targetIndexStart + 1], sizeof(SDL_Color));

    float distance = fabsf(waterfallThresholds[targetIndexStart]) + waterfallThresholds[targetIndexStart + 1];
    float pointPos = fabsf(waterfallThresholds[targetIndexStart]) + db;
    float pointPct = pointPos / distance;

    retVal.r = (Uint8)floorf((float)start.r + pointPct * ((float)stop.r - (float)start.r));
    retVal.g = (Uint8)floorf((float)start.g + pointPct * ((float)stop.g - (float)start.g));
    retVal.b = (Uint8)floorf((float)start.b + pointPct * ((float)stop.b - (float)start.b));
    retVal.a = 255;

    return retVal;
}

void gA_UpdateWaterfallScaleFactor(float targetWidth, float fftSize) {
    currentWaterfallFftFactor = fftSize / targetWidth;
}

Uint32 gA_GetPixel(SDL_Surface *surface, int x, int y) {
    int bpp = surface->format->BytesPerPixel;
    /* Here p is the address to the pixel we want to retrieve */
    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
        case 1:
            return *p;
            break;

        case 2:
            return *(Uint16 *)p;
            break;

        case 3:
            if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
                return p[0] << 16 | p[1] << 8 | p[2];
            else
                return p[0] | p[1] << 8 | p[2] << 16;
            break;

        case 4:
            return *(Uint32 *)p;
            break;

        default:
            return 0;       /* shouldn't happen, but avoids warnings */
    }
}

SDL_Color *gA_FetchImgPalette(const char *imgPath, size_t numColors) {
    if (!imgPath || (numColors < 1 || numColors > 256))
        return NULL;

    size_t len = 0;
#ifdef WIN32
    char pwd[MAX_PATH];
    _getcwd(pwd, MAX_PATH);

    len = snprintf(NULL, 0, "%s\\ffmpeg.exe -i '%s' -vf palettegen=\"max_colors=%zu\" -c:v png -f image2pipe -",
                   pwd, imgPath, numColors);
#else
    len = snprintf(NULL, 0, "/bin/ffmpeg -i '%s' -vf palettegen=\"max_colors=%zu\" -c:v png -f image2pipe -",
                   imgPath, numColors);
#endif

    if (len < 1)
        return NULL;

    char *argString = malloc(len + 1);

    if (!argString)
        return NULL;

    memset(argString, 0, len + 1);

#ifdef WIN32
    snprintf(argString, len + 1,
             "%s\\ffmpeg.exe -i \"%s\" -vf palettegen=\"max_colors=%zu\" -c:v png -f image2pipe -",
             pwd, imgPath, numColors);
#else
    snprintf(argString, len + 1,
             "/bin/ffmpeg -i \"%s\" -vf palettegen=\"max_colors=%zu\" -c:v png -f image2pipe -",
             imgPath, numColors);
#endif

    FILE *fp;
    char *imgBuffer = malloc(GA_IMGPAL_MAXSZ);

    if (!imgBuffer) {
        free(argString);
        return NULL;
    }

    memset(imgBuffer, 0, GA_IMGPAL_MAXSZ);

#ifdef WIN32
    fp = _popen(argString, "rb");
#else
    fp = popen(argString, "rb");
#endif

    if (!fp) {
        free(argString);
        return NULL;
    }

    size_t imgBufferPos = 0;
    size_t lastReadSz = fread(imgBuffer, 1, 1024, fp);
    do {
        imgBufferPos += lastReadSz;
        lastReadSz = fread(imgBuffer + imgBufferPos, 1, 1024, fp);
    } while (lastReadSz == 1024);

    imgBufferPos += lastReadSz;
    fclose(fp);
    free(argString);

    if (imgBufferPos == 0) {
        return NULL;
    }

    SDL_RWops *rw = SDL_RWFromMem(imgBuffer, (int)imgBufferPos);
    SDL_Surface *tempSurface = IMG_Load_RW(rw, 1);

    SDL_Color *retVal = malloc(sizeof(SDL_Color) * numColors);
    if (!retVal) {
        SDL_FreeSurface(tempSurface);
        return NULL;
    }

    if (SDL_LockSurface(tempSurface) != 0) {
        SDL_FreeSurface(tempSurface);
        return NULL;
    }

    int c = 0;
    for (int y = 0; y < 16 && c < numColors; ++y) {
        for (int x = 0; x < 16 && c < numColors; ++x) {
            Uint32 colorRaw = gA_GetPixel(tempSurface, x, y);
            SDL_GetRGBA(colorRaw, tempSurface->format,
                        &retVal[c].r, &retVal[c].g, &retVal[c].b, &retVal[c].a);
            x++;
            c++;
        }
        y++;
    }

    SDL_UnlockSurface(tempSurface);
    SDL_FreeSurface(tempSurface);

    // Sort palette by subjective brightness, darkest to brightest.
    float brightnessVals[256];
    float swapF;
    SDL_Color swapC;

    for (int i = 0; i < numColors; ++i) {
        brightnessVals[i] = (0.21f * (float)retVal[i].r) + (0.72f * (float)retVal[i].g) + (0.07f * (float)retVal[i].b);
    }

    for (c = 0; c < (numColors - 1); ++c) {
        for (int d = 0; d < numColors - c - 1; ++d) {
            if (brightnessVals[d] > brightnessVals[d + 1]) {
                swapF = brightnessVals[d];
                swapC = retVal[d];
                brightnessVals[d] = brightnessVals[d + 1];
                retVal[d] = retVal[d + 1];
                brightnessVals[d + 1] = swapF;
                retVal[d + 1] = swapC;
            }
        }
    }

    return retVal;
}

void gA_Teardown() {
    if (mainRenderer)
        SDL_DestroyRenderer(mainRenderer);

    if (mainWindow)
        SDL_DestroyWindow(mainWindow);

    if (bassMixStream) {
        BASS_StreamFree(bassMixStream);
    }

    if (BASS_IsStarted() && BASS_Stop())
        BASS_Free();
}

bool gA_Setup() {
#ifdef WIN32
    char pwd[MAX_PATH];
    _getcwd(pwd, MAX_PATH);
#else
    char pwd[PATH_MAX];
    getcwd(pwd, PATH_MAX);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS |
                 SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC |
                 SDL_INIT_GAMECONTROLLER | SDL_INIT_SENSOR)) {
        fprintf(stderr, "Unable to start SDL: %s", SDL_GetError());
        return false;
    }

    if (SDL_GetDisplayDPI(0, &ddpi, &hdpi, &vdpi) < 0) {
        fprintf(stderr, "Unable to get primary display DPI: %s", SDL_GetError());
        return false;
    }

    currentScrollRateTarget = vdpi * 0.5f;
    gA_UpdateWaterfallScaleFactor(GA_WIDTH, GA_FFT_SIZE);

    mainWindow = SDL_CreateWindow(GA_TITLE, GA_START_POS_X,
                                  GA_START_POS_Y, GA_WIDTH, GA_HEIGHT, 0);

    if (!mainWindow) {
        fprintf(stderr, "Unable to create window: %s", SDL_GetError());
        return false;
    }

#ifdef WIN32
    SDL_VERSION(&mainWindowInfo.version);
    if (SDL_GetWindowWMInfo(mainWindow, &mainWindowInfo) == SDL_FALSE) {
        fprintf(stderr, "Unable to fetch window info: %s", SDL_GetError());
        return false;
    }
#endif

    mainRenderer = SDL_CreateRenderer(mainWindow, -1,
                                      SDL_RENDERER_ACCELERATED |SDL_RENDERER_PRESENTVSYNC);
    if (!mainRenderer) {
        fprintf(stderr, "Unable to create renderer: %s", SDL_GetError());
        return false;
    }

    mainWindowSurface = SDL_GetWindowSurface(mainWindow);

#ifdef WIN32
    if (!BASS_Init(-1, 48000, BASS_DEVICE_STEREO, mainWindowInfo.info.win.window, NULL)) {
#else
    if (!BASS_Init(-1, 48000, BASS_DEVICE_STEREO, NULL, NULL)) {
#endif
        fprintf(stderr, "BASS_Init returned error code: %i", BASS_ErrorGetCode());
        return false;
    }

    bassMixStream = BASS_Mixer_StreamCreate(48000, 2, 0);
    if (bassMixStream == 0) {
        fprintf(stderr, "Unable to initialize BASS mixer, error code: %i", BASS_ErrorGetCode());
        return false;
    }

#ifdef WIN32
    size_t len = snprintf(NULL, 0, "%s\\resources\\album_art.png", pwd);
#else
    size_t len = snprintf(NULL, 0, "%s/resources/album_art.png", pwd);
#endif

    if (len < 1) {
        fprintf(stderr, "Unable to build path to album art.");
        return false;
    }

    char *albumArtPath = malloc(len + 1);

    if (!albumArtPath) {
        fprintf(stderr, "Unable to build path to album art.");
        return false;
    }

#ifdef WIN32
    snprintf(albumArtPath, len + 1, "%s\\resources\\album_art.png", pwd);
#else
    snprintf(albumArtPath, len + 1, "%s/resources/album_art.png", pwd);
#endif

    albumArt = IMG_Load(albumArtPath);

    if (!albumArt) {
        fprintf(stderr, "Unable to load album art: %s", SDL_GetError());
        free(albumArtPath);
        return false;
    }

    currentPalette = gA_FetchImgPalette(albumArtPath, GA_WF_NUM_COLORS);
    free(albumArtPath);

    if (!currentPalette)
        return false;

#ifdef WIN32
    len = snprintf(NULL, 0, "%s\\resources\\audio.mp3", pwd);
#else
    len = snprintf(NULL, 0, "%s/resources/audio.mp3", pwd);
#endif

    if (len < 1) {
        fprintf(stderr, "Unable to build path to audio file.");
        return false;
    }

    char *audioFile = malloc(len + 1);

    if (!audioFile) {
        fprintf(stderr, "Unable to build path to audio file.");
        return false;
    }

#ifdef WIN32
    snprintf(audioFile, len + 1, "%s\\resources\\audio.mp3", pwd);
#else
    snprintf(audioFile, len + 1, "%s/resources/audio.mp3", pwd);
#endif

    musicStream = BASS_StreamCreateFile(false,
                                        audioFile,
                                        0,
                                        0,
                                        BASS_STREAM_DECODE | BASS_SAMPLE_LOOP);

    free(audioFile);

    if (musicStream == 0) {
        fprintf(stderr, "Unable to load audio file.");
        return false;
    }

    BASS_Mixer_StreamAddChannel(bassMixStream, musicStream, 0);
    frameLimit = 1000.0f / 120.0f;

    gA_UseFpsManager();

    return true;
}

#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
#ifdef __linux__
__asm__(".symver realpath,realpath@GLIBC_2.2.5");
#endif
int main() {
    if (!gA_Setup()) {
        gA_Teardown();
        return 1;
    }

    BASS_ChannelPlay(bassMixStream, false);
    int waterfallYPos = (int)floorf(GA_HEIGHT / 3.0f);

    while (true) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // TODO: Who knows.
        }

        if (BASS_ChannelIsActive(bassMixStream) != BASS_ACTIVE_PLAYING)
            BASS_ChannelPlay(bassMixStream, false);

        SDL_SetRenderDrawColor(mainRenderer,
                               currentPalette[0].r,
                               currentPalette[0].g,
                               currentPalette[0].b,
                               currentPalette[0].a);
        SDL_RenderClear(mainRenderer);

        float currentStride = (lastDelta / 1000.0f) * currentScrollRateTarget;
        SDL_FRect rect = {0};
        SDL_Color color;
        float fft[GA_FFT_SIZE] = {0};

        BASS_ChannelGetData(bassMixStream, fft, BASS_DATA_FFT16384);

        if (waterfallTexture) {
            SDL_Rect srcRect;
            SDL_FRect dstRect;

            srcRect.x = 0;
            srcRect.y = waterfallYPos;
            srcRect.w = GA_WIDTH;
            srcRect.h = GA_HEIGHT - waterfallYPos;

            dstRect.x = 0.0f;
            dstRect.y = (float)waterfallYPos + (currentStride < 1.0f ? 1.0f : currentStride);
            dstRect.w = (float)srcRect.w;
            dstRect.h = (float)srcRect.h;
            SDL_RenderCopyF(mainRenderer, waterfallTexture, &srcRect, &dstRect);
        }

        int bucketsLeft = GA_FFT_SIZE;
        int bucket = 0;
        for (int i = 0; i < GA_WIDTH; ++i) {
            int offset = 0;
            float sumMags = 0.0f;
            while (offset < (int)currentWaterfallFftFactor && offset < bucketsLeft) {
                sumMags += fft[bucket + offset];
                bucketsLeft--;
                offset++;
            }
            bucket += offset;
            sumMags /= (float)offset;
            color = gA_GetColorForMagnitude(sumMags, currentPalette);
            rect.y = (float)waterfallYPos;
            rect.x = (float)i;
            rect.w = 1.0f;
            rect.h = currentStride;
            SDL_SetRenderDrawColor(mainRenderer, color.r, color.g, color.b, color.a);
            SDL_RenderDrawRectF(mainRenderer, &rect);
        }

        if (waterfallTexture)
            SDL_DestroyTexture(waterfallTexture);


        SDL_Surface *currentScreen = SDL_CreateRGBSurface(0, GA_WIDTH, GA_HEIGHT, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000);
        SDL_RenderReadPixels(mainRenderer, NULL, SDL_PIXELFORMAT_ARGB8888, currentScreen->pixels, currentScreen->pitch);

        waterfallTexture = SDL_CreateTextureFromSurface(mainRenderer, currentScreen);
        SDL_FreeSurface(currentScreen);

        SDL_RenderPresent(mainRenderer);
        gA_UseFpsManager();
    }
    return 0;
}
#pragma clang diagnostic pop
