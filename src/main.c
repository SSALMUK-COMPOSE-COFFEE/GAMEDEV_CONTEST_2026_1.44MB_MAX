
#include "raylib.h"
#include <stddef.h>
#include <stdlib.h>

#define GRID_W 18
#define GRID_H 10
#define GRID_N (GRID_W * GRID_H)
#define CELL 40
#define GRID_X 120
#define GRID_Y 170
#define SCREEN_W 960
#define SCREEN_H 660

#define MAX_FILES 8
#define FILE_MIN 2
#define FILE_MAX 5

enum { EMPTY = 0, BLOCK, BAD, CLEARING };

typedef struct {
    unsigned char kind;
    unsigned char file;
    float flash;
} Cell;

enum { FT_NORMAL = 0, FT_VOLATILE, FT_PINNED };

typedef struct {
    int size;
    int alive;
    int type;
    int pinned;
    float life;
    float lifeMax;
} FileRec;

typedef enum { SC_BOOT, SC_TITLE, SC_PLAY, SC_OVER } Screen;

static Cell grid[GRID_N];
static FileRec files[MAX_FILES];

static Screen screen;
static int headX, headY;
static int carrying;
static int score, best;
static int combo;
static float comboT;
static float writeT, writeInterval;
static float playT;
static float shake;
static float moveT;
static int compacted;
static int repairFill;
static int seenVolatile, seenPinned;
static const char *noticeMsg;
static float noticeT;
static unsigned int rngState;

enum { ASSIST_FULL = 0, ASSIST_CARRY_ONLY, ASSIST_OFF };

static int assistLevel(void)
{
    if (compacted < 6)  return ASSIST_FULL;
    if (compacted < 15) return ASSIST_CARRY_ONLY;
    return ASSIST_OFF;
}

static const Color FILE_COL[8] = {
    {  84, 200, 255, 255 },
    { 255, 190,  80, 255 },
    { 130, 235, 130, 255 },
    { 255, 120, 160, 255 },
    { 190, 150, 255, 255 },
    { 255, 240, 120, 255 },
    { 120, 255, 225, 255 },
    { 255, 140,  90, 255 },
};

#define MAX_PARTICLES 160
#define MAX_POPS 8

typedef struct {
    float x, y, vx, vy, life, maxLife;
    Color c;
} Particle;

typedef struct {
    float x, y, life;
    int value;
    Color c;
} Pop;

static Particle parts[MAX_PARTICLES];
static int partNext;
static Pop pops[MAX_POPS];
static int popNext;
static int paused;
static float idleT;
static float titleT;
static float bootT;
static float transT;
static Screen prevScreen;
static int crt = 1;
static float shownScore;
static RenderTexture2D target;
static int wasFocused = 1;
static float hitstop;
static float headVX, headVY;

static const Color BG        = {  10,  14,  28, 255 };
static const Color PANEL     = {  20,  28,  50, 255 };
static const Color SECTOR    = {  26,  34,  60, 255 };
static const Color SECTOR_LN = {  38,  50,  84, 255 };
static const Color BADCOL    = {  90,  30,  40, 255 };
static const Color TEXTCOL   = { 200, 216, 255, 255 };
static const Color DIMTEXT   = { 110, 130, 175, 255 };

static unsigned int rnd(void)
{
    rngState = rngState * 1664525u + 1013904223u;
    return rngState >> 8;
}

static int rndRange(int lo, int hi)
{
    return lo + (int)(rnd() % (unsigned int)(hi - lo + 1));
}

#define SR 22050
#define STEP_SAMPLES (SR * 60 / (132 * 4))

enum { OSC_SQUARE = 0, OSC_TRI, OSC_NOISE };

static Sound sfxPick, sfxDrop, sfxRot, sfxOver, sfxAssist, sfxDeny, sfxRepair;
static Sound sfxCompact[4];
static AudioStream musicStream;
static int audioReady;
static int muted;
static unsigned long long musicSample;
static unsigned int noiseState = 0x2545f491u;

static const float SEMI[12] = {
    32.703f, 34.648f, 36.708f, 38.891f, 41.203f, 43.654f,
    46.249f, 48.999f, 51.913f, 55.000f, 58.270f, 61.735f
};

static float noteFreq(int idx)
{
    float f = SEMI[idx % 12];
    for (int o = idx / 12; o > 0; o--) f *= 2.0f;
    return f;
}

static float noiseSample(void)
{
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (float)((int)(noiseState & 0xffff) - 32768) / 32768.0f;
}

static float osc(int type, float phase)
{
    if (type == OSC_SQUARE) return (phase < 0.5f) ? 1.0f : -1.0f;
    if (type == OSC_TRI) {
        float t = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
        return t * 2.0f - 1.0f;
    }
    return noiseSample();
}

static Sound renderTone(const float *f0, const float *f1, const int *type,
                        int segments, float segDur, float vol)
{
    int segFrames = (int)(segDur * SR);
    int frames = segFrames * segments;
    short *data = (short *)malloc((size_t)frames * sizeof(short));
    if (!data) { Sound s = {0}; return s; }

    float phase = 0.0f;
    for (int seg = 0; seg < segments; seg++) {
        for (int i = 0; i < segFrames; i++) {
            float t = (float)i / (float)segFrames;
            float freq = f0[seg] + (f1[seg] - f0[seg]) * t;
            phase += freq / (float)SR;
            while (phase >= 1.0f) phase -= 1.0f;

            float env = 1.0f - t;
            env = env * env;
            float v = osc(type[seg], phase) * env * vol;
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            data[seg * segFrames + i] = (short)(v * 32000.0f);
        }
    }

    Wave w = {0};
    w.frameCount = (unsigned int)frames;
    w.sampleRate = SR;
    w.sampleSize = 16;
    w.channels = 1;
    w.data = data;

    Sound s = LoadSoundFromWave(w);
    free(data);
    return s;
}

static void musicCallback(void *buffer, unsigned int frames)
{
    static const unsigned char BASS[4] = { 9, 5, 0, 7 };
    static const unsigned char ARP[4][3] = {
        { 9, 12, 16 }, { 5, 9, 12 }, { 0, 4, 7 }, { 7, 11, 14 }
    };
    static float leadPhase, bassPhase;
    static float leadEnv, bassEnv, hatEnv;
    static float leadFreq = 440.0f, bassFreq = 110.0f;
    static int lastStep = -1;

    short *out = (short *)buffer;

    for (unsigned int i = 0; i < frames; i++) {
        int step = (int)(musicSample / STEP_SAMPLES);
        if (step != lastStep) {
            lastStep = step;
            int s = step % 64;
            int bar = s / 16;
            leadFreq = noteFreq(ARP[bar][(s / 2) % 3] + 36);
            leadEnv = 0.20f;
            if (s % 8 == 0) {
                bassFreq = noteFreq(BASS[bar] + 12);
                bassEnv = 0.32f;
            }
            if (s % 4 == 2) hatEnv = 0.10f;
        }

        leadPhase += leadFreq / (float)SR;
        while (leadPhase >= 1.0f) leadPhase -= 1.0f;
        bassPhase += bassFreq / (float)SR;
        while (bassPhase >= 1.0f) bassPhase -= 1.0f;

        float v = osc(OSC_SQUARE, leadPhase) * leadEnv
                + osc(OSC_TRI, bassPhase) * bassEnv
                + noiseSample() * hatEnv;

        leadEnv -= leadEnv * 7.0f / (float)SR;
        bassEnv -= bassEnv * 4.0f / (float)SR;
        hatEnv -= hatEnv * 45.0f / (float)SR;

        if (muted) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        out[i] = (short)(v * 26000.0f);
        musicSample++;
    }
}

static void playSfx(Sound s)
{
    if (audioReady && !muted) PlaySound(s);
}

static void initAudio(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    audioReady = 1;

    float a[4], b[4];
    int t[4];

    a[0] = 520.0f;  b[0] = 900.0f;  t[0] = OSC_SQUARE;
    sfxPick = renderTone(a, b, t, 1, 0.07f, 0.35f);

    a[0] = 420.0f;  b[0] = 200.0f;  t[0] = OSC_SQUARE;
    sfxDrop = renderTone(a, b, t, 1, 0.06f, 0.30f);

    a[0] = 900.0f;  b[0] = 120.0f;  t[0] = OSC_NOISE;
    sfxRot = renderTone(a, b, t, 1, 0.45f, 0.40f);

    a[0] = 330.0f;  b[0] = 60.0f;   t[0] = OSC_TRI;
    sfxOver = renderTone(a, b, t, 1, 1.10f, 0.50f);

    a[0] = 700.0f;  b[0] = 1000.0f; t[0] = OSC_SQUARE;
    sfxAssist = renderTone(a, b, t, 1, 0.18f, 0.25f);

    a[0] = 200.0f;  b[0] = 110.0f;  t[0] = OSC_SQUARE;
    sfxDeny = renderTone(a, b, t, 1, 0.10f, 0.28f);

    for (int s = 0; s < 3; s++) {
        a[s] = 520.0f + (float)s * 190.0f;
        b[s] = a[s];
        t[s] = OSC_TRI;
    }
    sfxRepair = renderTone(a, b, t, 3, 0.075f, 0.40f);

    for (int k = 0; k < 4; k++) {
        float base = 440.0f;
        for (int s = 0; s < k; s++) base *= 1.335f;
        for (int s = 0; s < 3; s++) {
            float mul = (s == 0) ? 1.0f : (s == 1 ? 1.26f : 1.5f);
            a[s] = base * mul;
            b[s] = base * mul;
            t[s] = OSC_SQUARE;
        }
        sfxCompact[k] = renderTone(a, b, t, 3, 0.055f, 0.34f);
    }

    musicStream = LoadAudioStream(SR, 16, 1);
    SetAudioStreamCallback(musicStream, musicCallback);
    PlayAudioStream(musicStream);
}

static void spawnParticle(float x, float y, float speed, Color c)
{
    Particle *p = &parts[partNext];
    partNext = (partNext + 1) % MAX_PARTICLES;

    p->x = x;
    p->y = y;
    p->vx = (float)(rndRange(-100, 100)) * 0.01f * speed;
    p->vy = (float)(rndRange(-100, 100)) * 0.01f * speed - speed * 0.35f;
    p->maxLife = 0.35f + (float)rndRange(0, 30) * 0.01f;
    p->life = p->maxLife;
    p->c = c;
}

static void spawnBurst(int cellX, int cellY, int n, float speed, Color c)
{
    float cx = (float)(GRID_X + cellX * CELL + CELL / 2);
    float cy = (float)(GRID_Y + cellY * CELL + CELL / 2);
    for (int i = 0; i < n; i++) spawnParticle(cx, cy, speed, c);
}

static void spawnPop(int cellX, int cellY, int value, Color c)
{
    Pop *p = &pops[popNext];
    popNext = (popNext + 1) % MAX_POPS;
    p->x = (float)(GRID_X + cellX * CELL + CELL / 2);
    p->y = (float)(GRID_Y + cellY * CELL);
    p->life = 0.9f;
    p->value = value;
    p->c = c;
}

static int freeCount(void)
{
    int n = 0;
    for (int i = 0; i < GRID_N; i++) if (grid[i].kind == EMPTY) n++;
    return n;
}

static int nextFileSize(void)
{
    int hi = 5;
    if (compacted >= 10) hi = 6;
    if (compacted >= 20) hi = 7;
    return rndRange(FILE_MIN, hi);
}

static int newFileId(void)
{
    for (int f = 0; f < MAX_FILES; f++) if (!files[f].alive) return f;
    return -1;
}

static int writeFile(int size)
{
    if (freeCount() < size) return 0;
    int f = newFileId();
    if (f < 0) return 1;

    int type = FT_NORMAL;
    if (compacted >= 3 && rndRange(0, 99) < 22) type = FT_VOLATILE;
    else if (compacted >= 8 && size >= 3 && rndRange(0, 99) < 25) type = FT_PINNED;

    files[f].size = size;
    files[f].alive = 1;
    files[f].type = type;
    files[f].pinned = -1;
    files[f].lifeMax = 24.0f - playT * 0.15f;
    if (files[f].lifeMax < 10.0f) files[f].lifeMax = 10.0f;
    if (type == FT_VOLATILE) files[f].lifeMax *= 0.5f;
    files[f].life = files[f].lifeMax;

    int spots[8];
    for (int placed = 0; placed < size; ) {
        int i = (int)(rnd() % GRID_N);
        if (grid[i].kind != EMPTY) continue;
        grid[i].kind = BLOCK;
        grid[i].file = (unsigned char)f;
        grid[i].flash = 0.35f;
        spots[placed] = i;
        placed++;
    }

    if (type == FT_PINNED) files[f].pinned = spots[rndRange(0, size - 1)];

    if (type == FT_VOLATILE && !seenVolatile) {
        seenVolatile = 1;
        noticeMsg = "VOLATILE FILE - HALF THE LIFE, TRIPLE THE SCORE";
        noticeT = 3.4f;
    }
    if (type == FT_PINNED && !seenPinned) {
        seenPinned = 1;
        noticeMsg = "PINNED BLOCK - IT CANNOT BE MOVED, BUILD AROUND IT";
        noticeT = 3.4f;
    }
    return 1;
}

static void rotFile(int f)
{
    for (int i = 0; i < GRID_N; i++) {
        if (grid[i].kind == BLOCK && grid[i].file == f) {
            grid[i].kind = BAD;
            grid[i].flash = 0.5f;
            spawnBurst(i % GRID_W, i / GRID_W, 7, 150.0f, (Color){200, 60, 70, 255});
        }
    }
    files[f].alive = 0;
    if (carrying == f) carrying = -1;
    combo = 0;
    comboT = 0;
    shake = 14.0f;
    hitstop = 0.10f;
    playSfx(sfxRot);
}

#define REPAIR_COST 14

static void runScandisk(void)
{
    int fixed = 0;
    for (int i = 0; i < GRID_N && fixed < 3; i++) {
        if (grid[i].kind != BAD) continue;
        grid[i].kind = CLEARING;
        grid[i].file = 2;
        grid[i].flash = 0.30f + (float)fixed * 0.12f;
        fixed++;
    }
    if (fixed == 0) return;

    repairFill -= REPAIR_COST;
    if (repairFill < 0) repairFill = 0;
    shake = 8.0f;
    playSfx(sfxRepair);
    noticeMsg = "SCANDISK - SECTORS RECOVERED";
    noticeT = 2.2f;
}

static void checkSorted(void)
{
    for (int f = 0; f < MAX_FILES; f++) {
        if (!files[f].alive) continue;
        int first = -1, last = -1, count = 0;
        for (int i = 0; i < GRID_N; i++) {
            if (grid[i].kind == BLOCK && grid[i].file == f) {
                if (first < 0) first = i;
                last = i;
                count++;
            }
        }
        if (count != files[f].size) continue;
        if (last - first != count - 1) continue;
        if (first / GRID_W != last / GRID_W) continue;

        for (int i = first; i <= last; i++) {
            grid[i].kind = CLEARING;
            grid[i].flash = 0.28f + (float)(i - first) * 0.07f;
        }
        files[f].alive = 0;

        combo++;
        comboT = 4.0f;
        int gained = count * count * 10 * combo;
        if (files[f].type == FT_VOLATILE) gained *= 3;
        score += gained;
        shake = 5.0f + (float)count * 1.5f;
        hitstop = 0.05f + (float)count * 0.012f;
        spawnPop(first % GRID_W, first / GRID_W, gained, FILE_COL[f & 7]);
        repairFill += count;
        if (repairFill >= REPAIR_COST) runScandisk();
        playSfx(sfxCompact[(combo - 1 > 3) ? 3 : combo - 1]);

        int before = assistLevel();
        compacted++;
        if (assistLevel() != before) {
            noticeMsg = (assistLevel() == ASSIST_OFF) ? "TARGET DISPLAY OFF"
                                                      : "TARGET DISPLAY: CARRYING ONLY";
            noticeT = 2.6f;
            playSfx(sfxAssist);
        }
    }
}

static int targetWindow(int f)
{
    int n = files[f].size;
    int headIdx = headY * GRID_W + headX;
    int best = -1, bestScore = -1;

    int pin = files[f].pinned;
    for (int s = 0; s + n <= GRID_N; s++) {
        if (s / GRID_W != (s + n - 1) / GRID_W) continue;
        if (pin >= 0 && (pin < s || pin >= s + n)) continue;

        int own = 0, ok = 1;
        for (int k = 0; k < n; k++) {
            Cell *c = &grid[s + k];
            if (c->kind == EMPTY) continue;
            if (c->kind == BLOCK && c->file == f) { own++; continue; }
            ok = 0;
            break;
        }
        if (!ok) continue;

        int dist = (s > headIdx) ? (s - headIdx) : (headIdx - s);
        int sc = own * 10000 - dist;
        if (sc > bestScore) { bestScore = sc; best = s; }
    }
    return best;
}

static int focusFile(void)
{
    if (assistLevel() == ASSIST_OFF) return -1;
    if (carrying >= 0) return carrying;
    if (assistLevel() == ASSIST_CARRY_ONLY) return -1;

    int i = headY * GRID_W + headX;
    if (grid[i].kind == BLOCK && files[grid[i].file].alive) return grid[i].file;

    int urgent = -1;
    for (int f = 0; f < MAX_FILES; f++) {
        if (!files[f].alive) continue;
        if (urgent < 0 || files[f].life < files[urgent].life) urgent = f;
    }
    return urgent;
}

static void resetGame(void)
{
    for (int i = 0; i < GRID_N; i++) { grid[i].kind = EMPTY; grid[i].flash = 0; }
    for (int f = 0; f < MAX_FILES; f++) files[f].alive = 0;

    headX = GRID_W / 2;
    headY = GRID_H / 2;
    carrying = -1;
    score = 0;
    shownScore = 0.0f;
    combo = 0;
    comboT = 0;
    playT = 0;
    shake = 0;
    moveT = 0;
    paused = 0;
    headVX = (float)headX;
    headVY = (float)headY;
    hitstop = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) parts[i].life = 0;
    for (int i = 0; i < MAX_POPS; i++) pops[i].life = 0;
    compacted = 0;
    repairFill = 0;
    seenVolatile = 0;
    seenPinned = 0;
    noticeMsg = NULL;
    noticeT = 0;
    writeInterval = 4.5f;
    writeT = 7.0f;

    writeFile(2);
}

static const short DEMO_FROM[3][3] = { { 5, 40, 77 }, { 12, 58, 95 }, { 30, 66, 122 } };
static const short DEMO_TO[3][3]   = { { 54, 55, 56 }, { 75, 76, 77 }, { 111, 112, 113 } };

static void drawTitleBackdrop(void)
{
    for (int y = 0; y * CELL < SCREEN_H; y++)
        for (int x = 0; x * CELL < SCREEN_W; x++)
            DrawRectangle(x * CELL + 1, y * CELL + 1,
                          CELL - 2, CELL - 2, (Color){18, 24, 44, 255});

    float cycle = titleT;
    while (cycle >= 6.0f) cycle -= 6.0f;
    float phase = cycle / 6.0f;

    float move = (phase - 0.10f) / 0.50f;
    if (move < 0.0f) move = 0.0f;
    if (move > 1.0f) move = 1.0f;

    for (int f = 0; f < 3; f++) {
        for (int k = 0; k < 3; k++) {
            int a = DEMO_FROM[f][k], b = DEMO_TO[f][k];
            float ax = (float)(a % GRID_W), ay = (float)(a / GRID_W);
            float bx = (float)(b % GRID_W), by = (float)(b / GRID_W);
            float px = ax + (bx - ax) * move;
            float py = ay + (by - ay) * move;

            Color c = FILE_COL[f];
            unsigned char alpha = 110;
            if (phase > 0.72f) {
                float k2 = (phase - 0.72f) / 0.16f;
                if (k2 > 1.0f) k2 = 1.0f;
                alpha = (unsigned char)(110.0f * (1.0f - k2));
                c = (Color){255, 255, 255, alpha};
            }
            DrawRectangle(GRID_X + (int)(px * CELL) + 4, GRID_Y + (int)(py * CELL) + 4,
                          CELL - 8, CELL - 8, (Color){c.r, c.g, c.b, alpha});
        }
    }

    int sweep = (int)(phase * (float)SCREEN_W);
    DrawRectangle(sweep, 0, 2, SCREEN_H, (Color){120, 200, 255, 55});

    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){10, 14, 28, 200});
}

static const char *BOOT_LINES[] = {
    "2P GAME ARCADE BIOS   v1.44",
    "",
    "DRIVE A:      3.5\" HD FLOPPY",
    "CAPACITY      1,474,560 BYTES",
    "",
    "SCANNING FILE ALLOCATION TABLE ...",
    "",
    "FRAGMENTATION            87%",
    "BAD SECTORS              NONE",
    "WRITE PRESSURE           RISING",
    "",
    "LOADING   DEFRAG.EXE"
};
#define BOOT_COUNT ((int)(sizeof(BOOT_LINES) / sizeof(BOOT_LINES[0])))
#define BOOT_STEP 0.17f

static void drawBoot(void)
{
    Color green = { 120, 255, 150, 255 };
    int shown = (int)(bootT / BOOT_STEP);
    if (shown > BOOT_COUNT) shown = BOOT_COUNT;

    for (int i = 0; i < shown; i++)
        DrawText(BOOT_LINES[i], 150, 120 + i * 26, 16, green);

    if (shown < BOOT_COUNT) {
        if (((int)(bootT * 3.0f) & 1) == 0)
            DrawRectangle(150, 132 + shown * 26, 10, 3, green);
    } else {
        if (((int)(bootT * 2.0f) & 1) == 0)
            DrawText("PRESS ANY KEY", 150, 132 + BOOT_COUNT * 26 + 12, 16, green);
    }
}

static void drawCrtOverlay(Rectangle d)
{
    for (int y = 0; y < (int)d.height; y += 3)
        DrawRectangle((int)d.x, (int)d.y + y, (int)d.width, 1, (Color){0, 0, 0, 46});

    int vw = (int)(d.width * 0.16f);
    int vh = (int)(d.height * 0.18f);
    Color dark = { 0, 0, 0, 130 };
    Color clear = { 0, 0, 0, 0 };
    DrawRectangleGradientV((int)d.x, (int)d.y, (int)d.width, vh, dark, clear);
    DrawRectangleGradientV((int)d.x, (int)(d.y + d.height) - vh, (int)d.width, vh, clear, dark);
    DrawRectangleGradientH((int)d.x, (int)d.y, vw, (int)d.height, dark, clear);
    DrawRectangleGradientH((int)(d.x + d.width) - vw, (int)d.y, vw, (int)d.height, clear, dark);
}

static void drawTransition(void)
{
    if (transT <= 0.0f) return;
    float p = 1.0f - transT / 0.40f;
    int x = (int)(p * (float)SCREEN_W);
    DrawRectangle(0, 0, x, SCREEN_H, (Color){10, 14, 28, (unsigned char)(120 * transT / 0.40f)});
    DrawRectangle(x - 3, 0, 6, SCREEN_H, (Color){140, 220, 255, 200});
}

static int actionPressed(void)
{
    return IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_ENTER) ||
           IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
}

static int dirHeld(int *dx, int *dy)
{
    *dx = 0; *dy = 0;
    if (IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  *dx = -1;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) *dx =  1;
    if (IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP))    *dy = -1;
    if (IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S) || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  *dy =  1;
    return (*dx != 0 || *dy != 0);
}

static int anyInputActive(void)
{
    int dx, dy;
    if (dirHeld(&dx, &dy)) return 1;
    if (actionPressed()) return 1;
    if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_M) || IsKeyPressed(KEY_ESCAPE) ||
        IsKeyPressed(KEY_F) || IsKeyPressed(KEY_F11) || IsKeyPressed(KEY_C)) return 1;
    if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) return 1;
    return 0;
}

static void updatePlay(float dt)
{
    playT += dt;

    int dx, dy;
    if (dirHeld(&dx, &dy)) {
        if (moveT <= 0.0f) {
            headX += dx; headY += dy;
            if (headX < 0) headX = 0;
            if (headY < 0) headY = 0;
            if (headX >= GRID_W) headX = GRID_W - 1;
            if (headY >= GRID_H) headY = GRID_H - 1;
            moveT = (moveT < -0.5f) ? 0.16f : 0.055f;
        }
        moveT -= dt;
    } else {
        moveT = -1.0f;
    }

    if (actionPressed()) {
        int i = headY * GRID_W + headX;
        if (carrying < 0) {
            if (grid[i].kind == BLOCK && files[grid[i].file].pinned == i) {
                grid[i].flash = 0.3f;
                shake = 3.0f;
                playSfx(sfxDeny);
            } else if (grid[i].kind == BLOCK) {
                carrying = grid[i].file;
                grid[i].kind = EMPTY;
                grid[i].flash = 0.2f;
                playSfx(sfxPick);
            }
        } else {
            if (grid[i].kind == EMPTY) {
                grid[i].kind = BLOCK;
                grid[i].file = (unsigned char)carrying;
                grid[i].flash = 0.25f;
                spawnBurst(headX, headY, 4, 70.0f, FILE_COL[carrying & 7]);
                playSfx(sfxDrop);
                carrying = -1;
                checkSorted();
            }
        }
    }

    writeT -= dt;
    if (writeT <= 0.0f) {
        if (!writeFile(nextFileSize())) {
            if (score > best) best = score;
            screen = SC_OVER;
            shake = 18.0f;
            playSfx(sfxOver);
            return;
        }
        writeInterval -= 0.06f;
        if (writeInterval < 1.2f) writeInterval = 1.2f;
        writeT = writeInterval;
    }

    for (int f = 0; f < MAX_FILES; f++) {
        if (!files[f].alive) continue;
        files[f].life -= dt;
        if (files[f].life <= 0.0f) rotFile(f);
    }

    if (comboT > 0.0f) { comboT -= dt; if (comboT <= 0.0f) combo = 0; }
    if (noticeT > 0.0f) noticeT -= dt;

    float k = dt * 22.0f;
    if (k > 1.0f) k = 1.0f;
    headVX += ((float)headX - headVX) * k;
    headVY += ((float)headY - headVY) * k;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &parts[i];
        if (p->life <= 0.0f) continue;
        p->life -= dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += 420.0f * dt;
    }
    for (int i = 0; i < MAX_POPS; i++) {
        if (pops[i].life <= 0.0f) continue;
        pops[i].life -= dt;
        pops[i].y -= 42.0f * dt;
    }
    if (shake > 0.0f)  { shake -= dt * 30.0f; if (shake < 0.0f) shake = 0.0f; }

    for (int i = 0; i < GRID_N; i++) {
        if (grid[i].flash > 0.0f) {
            grid[i].flash -= dt;
            if (grid[i].flash <= 0.0f) {
                grid[i].flash = 0.0f;
                if (grid[i].kind == CLEARING) {
                    spawnBurst(i % GRID_W, i / GRID_W, 6, 130.0f,
                               FILE_COL[grid[i].file & 7]);
                    grid[i].kind = EMPTY;
                }
            }
        }
    }
}

static void drawCellRect(int x, int y, int inset, Color c)
{
    DrawRectangle(GRID_X + x * CELL + inset, GRID_Y + y * CELL + inset,
                  CELL - inset * 2, CELL - inset * 2, c);
}

static void drawGrid(void)
{
    DrawRectangle(GRID_X - 10, GRID_Y - 10,
                  GRID_W * CELL + 20, GRID_H * CELL + 20, PANEL);
    DrawRectangleLines(GRID_X - 10, GRID_Y - 10,
                       GRID_W * CELL + 20, GRID_H * CELL + 20, SECTOR_LN);

    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            int i = y * GRID_W + x;
            Cell *c = &grid[i];

            drawCellRect(x, y, 1, SECTOR);

            if (paused) continue;
            if (c->kind == BAD) {
                drawCellRect(x, y, 3, BADCOL);
                DrawLine(GRID_X + x * CELL + 8,  GRID_Y + y * CELL + 8,
                         GRID_X + x * CELL + 24, GRID_Y + y * CELL + 24, (Color){160, 60, 70, 255});
                DrawLine(GRID_X + x * CELL + 24, GRID_Y + y * CELL + 8,
                         GRID_X + x * CELL + 8,  GRID_Y + y * CELL + 24, (Color){160, 60, 70, 255});
            } else if (c->kind == BLOCK) {
                Color col = FILE_COL[c->file & 7];

                FileRec *fr = &files[c->file & 7];
                if (fr->alive && fr->lifeMax > 0.0f) {
                    float t = fr->life / fr->lifeMax;
                    if (t < 0.45f) {
                        float k = 1.0f - t / 0.45f;
                        float pulse = 0.55f + 0.45f * (float)((int)(playT * 8.0f) & 1);
                        float d = 1.0f - k * 0.55f * pulse;
                        col.r = (unsigned char)(col.r * d + 60 * (1.0f - d));
                        col.g = (unsigned char)(col.g * d);
                        col.b = (unsigned char)(col.b * d);
                    }
                }

                if (c->flash > 0.0f) {
                    float k = c->flash / 0.35f;
                    if (k > 1.0f) k = 1.0f;
                    col.r = (unsigned char)(col.r + (255 - col.r) * k);
                    col.g = (unsigned char)(col.g + (255 - col.g) * k);
                    col.b = (unsigned char)(col.b + (255 - col.b) * k);
                }
                drawCellRect(x, y, 3, col);

                if (fr->alive && fr->type == FT_VOLATILE) {
                    float p2 = 0.45f + 0.55f * (float)((int)(playT * 9.0f) & 1);
                    DrawRectangle(GRID_X + x * CELL + 15, GRID_Y + y * CELL + 15, 10, 10,
                                  (Color){255, 255, 255, (unsigned char)(200 * p2)});
                }
                if (fr->alive && fr->pinned == i) {
                    DrawRectangleLinesEx(
                        (Rectangle){(float)(GRID_X + x * CELL + 3),
                                    (float)(GRID_Y + y * CELL + 3), CELL - 6, CELL - 6},
                        3, (Color){20, 24, 40, 255});
                    DrawRectangle(GRID_X + x * CELL + 17, GRID_Y + y * CELL + 17, 6, 6,
                                  (Color){20, 24, 40, 255});
                }

                if (x > 0 && grid[i - 1].kind == BLOCK && grid[i - 1].file == c->file) {
                    DrawRectangle(GRID_X + x * CELL - 5, GRID_Y + y * CELL + CELL / 2 - 5,
                                  10, 10, col);
                }
            } else if (c->kind == CLEARING) {
                float k = c->flash / 0.30f;
                if (k < 0.0f) k = 0.0f;
                drawCellRect(x, y, 3, (Color){255, 255, 255, (unsigned char)(255 * k)});
            }
        }
    }

    int ff = paused ? -1 : focusFile();
    if (ff >= 0) {
        int s = targetWindow(ff);
        if (s >= 0) {
            Color fc = FILE_COL[ff & 7];
            float pulse = 0.5f + 0.5f * (float)((int)(playT * 4.0f) & 1);
            for (int k = 0; k < files[ff].size; k++) {
                int i = s + k;
                int x = i % GRID_W, y = i / GRID_W;
                if (grid[i].kind == BLOCK && grid[i].file == ff) continue;
                DrawRectangle(GRID_X + x * CELL + 3, GRID_Y + y * CELL + 3,
                              CELL - 6, CELL - 6,
                              (Color){fc.r, fc.g, fc.b, (unsigned char)(70 + 55 * pulse)});
                DrawRectangleLinesEx(
                    (Rectangle){(float)(GRID_X + x * CELL + 3), (float)(GRID_Y + y * CELL + 3),
                                CELL - 6, CELL - 6}, 3,
                    (Color){255, 255, 255, (unsigned char)(150 + 105 * pulse)});
            }
        }
    }

    int hx = GRID_X + (int)(headVX * CELL);
    int hy = GRID_Y + (int)(headVY * CELL);
    Color hc = (carrying >= 0) ? FILE_COL[carrying & 7] : (Color){255, 255, 255, 255};
    if (!paused) {
        DrawRectangleLinesEx((Rectangle){(float)hx - 2, (float)hy - 2, CELL + 4, CELL + 4}, 2, hc);
        if (carrying >= 0)
            DrawRectangle(hx + 9, hy + 9, CELL - 18, CELL - 18, hc);
    }

    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &parts[i];
        if (p->life <= 0.0f || paused) continue;
        float t = p->life / p->maxLife;
        int sz = 2 + (int)(4.0f * t);
        DrawRectangle((int)p->x - sz / 2, (int)p->y - sz / 2, sz, sz,
                      (Color){p->c.r, p->c.g, p->c.b, (unsigned char)(255 * t)});
    }

    for (int i = 0; i < MAX_POPS; i++) {
        Pop *p = &pops[i];
        if (p->life <= 0.0f || paused) continue;
        const char *txt = TextFormat("+%d", p->value);
        int w = MeasureText(txt, 20);
        float t = p->life / 0.9f;
        DrawText(txt, (int)p->x - w / 2, (int)p->y, 20,
                 (Color){p->c.r, p->c.g, p->c.b, (unsigned char)(255 * (t > 1.0f ? 1.0f : t))});
    }

    if (!paused)
        DrawRectangle(GRID_X - 10, hy + CELL / 2 - 1, GRID_W * CELL + 20, 1,
                      (Color){hc.r, hc.g, hc.b, 40});
}

static void drawHud(void)
{
    DrawText("D E F R A G", 96, 34, 40, TEXTCOL);
    DrawText("1,474,560 BYTES", 96, 80, 10, DIMTEXT);

    DrawText(TextFormat("SCORE %d", (int)shownScore), 420, 40, 20, TEXTCOL);
    DrawText(TextFormat("BEST %d", best), 420, 66, 10, DIMTEXT);

    if (combo > 1) {
        DrawText(TextFormat("x%d", combo), 420, 88, 20,
                 FILE_COL[(combo - 1) & 7]);
    }

    int fr = freeCount();
    int pct = fr * 100 / GRID_N;
    DrawText("FREE", 640, 40, 10, DIMTEXT);
    DrawRectangle(640, 56, 220, 14, SECTOR);
    DrawRectangle(640, 56, 220 * fr / GRID_N, 14,
                  (pct < 15) ? (Color){255, 90, 90, 255} : (Color){90, 200, 140, 255});
    DrawRectangleLines(640, 56, 220, 14, SECTOR_LN);
    DrawText(TextFormat("%d SECTORS  (%d%%)", fr, pct), 640, 76, 10, DIMTEXT);

    int rf = repairFill;
    if (rf > REPAIR_COST) rf = REPAIR_COST;
    DrawText("SCANDISK", 640, 92, 10, DIMTEXT);
    DrawRectangle(716, 92, 144, 10, SECTOR);
    DrawRectangle(716, 92, 144 * rf / REPAIR_COST, 10, (Color){120, 200, 255, 255});
    DrawRectangleLines(716, 92, 144, 10, SECTOR_LN);

    int col = GRID_X;
    for (int f = 0; f < MAX_FILES; f++) {
        if (!files[f].alive) continue;
        int y = 106;
        float t = (files[f].lifeMax > 0.0f) ? files[f].life / files[f].lifeMax : 0.0f;
        if (t < 0.0f) t = 0.0f;

        if (col > SCREEN_W - 140) break;

        for (int k = 0; k < files[f].size; k++) {
            DrawRectangle(col + k * 9, y, 7, 12, FILE_COL[f & 7]);
            if (files[f].type == FT_VOLATILE)
                DrawRectangle(col + k * 9 + 2, y + 4, 3, 4, (Color){255, 255, 255, 255});
        }
        if (files[f].pinned >= 0)
            DrawRectangle(col, y + 14, files[f].size * 9 - 2, 2, (Color){140, 160, 210, 255});

        int barX = col + files[f].size * 9 + 4;
        DrawRectangle(barX, y + 2, 30, 8, SECTOR);
        DrawRectangle(barX, y + 2, (int)(30 * t), 8,
                      (t < 0.35f) ? (Color){255, 90, 90, 255} : FILE_COL[f & 7]);

        col = barX + 30 + 22;
    }

    const char *hint = (assistLevel() == ASSIST_OFF)
        ? "PACK EACH FILE INTO ONE UNBROKEN RUN ON A SINGLE TRACK"
        : "CARRY EACH BLOCK INTO THE GLOWING SLOTS OF ITS OWN COLOUR";
    DrawText(hint, GRID_X, GRID_Y + GRID_H * CELL + 22, 14,
             (carrying >= 0) ? FILE_COL[carrying & 7] : TEXTCOL);
    DrawText("MOVE: ARROWS / WASD   PICK & DROP: SPACE   P: PAUSE   M: SOUND   ESC: QUIT",
             GRID_X, GRID_Y + GRID_H * CELL + 44, 10, DIMTEXT);
    DrawText(muted ? "SOUND OFF" : "SOUND ON", SCREEN_W - 200, 116, 10,
             muted ? (Color){255, 110, 110, 255} : DIMTEXT);

    if (noticeT > 0.0f && noticeMsg) {
        int w = MeasureText(noticeMsg, 20);
        unsigned char a = (unsigned char)(255 * ((noticeT > 1.0f) ? 1.0f : noticeT));
        DrawText(noticeMsg, SCREEN_W / 2 - w / 2, GRID_Y - 34, 20,
                 (Color){255, 240, 120, a});
    }
}

static void drawTitle(void)
{
    DrawText("D E F R A G", SCREEN_W / 2 - 150, 180, 50, TEXTCOL);
    DrawText("the disk is fragmenting. put the files back together.",
             SCREEN_W / 2 - 210, 250, 12, DIMTEXT);

    DrawText("MOVE THE HEAD", 300, 330, 12, DIMTEXT);
    DrawText("ARROWS / WASD / D-PAD", 540, 330, 12, TEXTCOL);
    DrawText("PICK UP  /  PUT DOWN", 300, 360, 12, DIMTEXT);
    DrawText("SPACE / Z / GAMEPAD A", 540, 360, 12, TEXTCOL);
    DrawText("FILL THE GLOWING SLOTS", 300, 390, 12, DIMTEXT);
    DrawText("THE FILE COMPACTS AWAY", 540, 390, 12, TEXTCOL);
    DrawText("LEAVE A FILE TOO LONG AND IT ROTS INTO DEAD SECTORS.", 300, 430, 12,
             (Color){255, 110, 110, 255});
    DrawText("COMPACT ENOUGH AND SCANDISK GIVES DEAD SECTORS BACK.", 300, 452, 12,
             (Color){120, 200, 255, 255});

    DrawText("PRESS SPACE TO START", SCREEN_W / 2 - 120, 500, 20, TEXTCOL);
    DrawText("F: FULLSCREEN     C: CRT     M: SOUND     ESC: QUIT",
             SCREEN_W / 2 - 145, 560, 10, DIMTEXT);
}

static void drawPaused(void)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){10, 14, 28, 190});
    DrawText("PAUSED", SCREEN_W / 2 - 95, 250, 42, TEXTCOL);
    DrawText("THE DISK IS HIDDEN WHILE PAUSED", SCREEN_W / 2 - 155, 310, 12, DIMTEXT);
    DrawText("P / START      RESUME", SCREEN_W / 2 - 110, 370, 14, TEXTCOL);
    DrawText("M              SOUND", SCREEN_W / 2 - 110, 396, 14, DIMTEXT);
    DrawText("ESC            QUIT TO TITLE", SCREEN_W / 2 - 110, 422, 14, DIMTEXT);
}

static void drawOver(void)
{
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){10, 14, 28, 200});
    DrawText("DISK FULL", SCREEN_W / 2 - 130, 230, 50, (Color){255, 110, 110, 255});
    DrawText(TextFormat("SCORE  %d", score), SCREEN_W / 2 - 90, 310, 24, TEXTCOL);
    DrawText(TextFormat("BEST   %d", best), SCREEN_W / 2 - 90, 344, 14, DIMTEXT);
    DrawText(TextFormat("SURVIVED  %d SECONDS", (int)playT), SCREEN_W / 2 - 110, 380, 12, DIMTEXT);
    DrawText("PRESS SPACE TO RETRY", SCREEN_W / 2 - 120, 460, 20, TEXTCOL);
}

int main(void)
{
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "DEFRAG");
    SetWindowMinSize(480, 330);
    target = LoadRenderTexture(SCREEN_W, SCREEN_H);
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
    initAudio();
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    rngState = 0x1234567u;
    screen = SC_BOOT;
    prevScreen = SC_BOOT;
    resetGame();
#ifdef DEFRAG_START_IN_PLAY
    screen = SC_PLAY;
#endif

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        titleT += dt;
        if (transT > 0.0f) transT -= dt;
        shownScore += ((float)score - shownScore) * (dt * 9.0f > 1.0f ? 1.0f : dt * 9.0f);
        if (IsKeyPressed(KEY_C)) crt = !crt;
        if (IsKeyPressed(KEY_M)) muted = !muted;
        if (IsKeyPressed(KEY_F) || IsKeyPressed(KEY_F11)) ToggleFullscreen();

        if (anyInputActive()) idleT = 0.0f;
        else idleT += dt;

        if (screen != prevScreen) { transT = 0.40f; prevScreen = screen; }
        if (screen == SC_PLAY && idleT > 30.0f) screen = SC_TITLE;
        if (screen == SC_OVER && idleT > 15.0f) screen = SC_TITLE;

        switch (screen) {
        case SC_BOOT:
            bootT += dt;
            if (anyInputActive() || bootT > BOOT_COUNT * BOOT_STEP + 2.2f) screen = SC_TITLE;
            break;
        case SC_TITLE:
            if (actionPressed()) { resetGame(); screen = SC_PLAY; }
            if (IsKeyPressed(KEY_ESCAPE)) goto quit;
            break;
        case SC_PLAY:
            if (IsKeyPressed(KEY_P) ||
                IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) paused = !paused;
            int focused = IsWindowFocused();
            if (wasFocused && !focused) paused = 1;
            wasFocused = focused;

            if (!paused) {
                if (hitstop > 0.0f) hitstop -= dt;
                else updatePlay(dt);
            }
            if (IsKeyPressed(KEY_ESCAPE)) screen = SC_TITLE;
            break;
        case SC_OVER:
            if (actionPressed()) { resetGame(); screen = SC_PLAY; }
            if (IsKeyPressed(KEY_ESCAPE)) screen = SC_TITLE;
            if (shake > 0.0f) { shake -= dt * 30.0f; if (shake < 0.0f) shake = 0.0f; }
            break;
        }

        BeginTextureMode(target);
        ClearBackground(BG);

        if (screen == SC_BOOT) {
            drawBoot();
        } else if (screen == SC_TITLE) {
            drawTitleBackdrop();
            drawTitle();
        } else {
            Camera2D cam = {0};
            cam.zoom = 1.0f;
            if (shake > 0.0f) {
                cam.offset.x = (float)(rndRange(-1, 1)) * shake * 0.5f;
                cam.offset.y = (float)(rndRange(-1, 1)) * shake * 0.5f;
            }
            BeginMode2D(cam);
            drawGrid();
            drawHud();
            EndMode2D();

            if (paused && screen == SC_PLAY) drawPaused();
            if (screen == SC_OVER) drawOver();

            float limit = (screen == SC_OVER) ? 15.0f : 30.0f;
            if (idleT > limit - 5.0f) {
                const char *msg = TextFormat("NO INPUT - RETURNING TO TITLE IN %d",
                                             (int)(limit - idleT) + 1);
                int w = MeasureText(msg, 16);
                DrawRectangle(0, SCREEN_H / 2 - 22, SCREEN_W, 44, (Color){10, 14, 28, 220});
                DrawText(msg, SCREEN_W / 2 - w / 2, SCREEN_H / 2 - 8, 16,
                         (Color){255, 240, 120, 255});
            }
        }
        drawTransition();
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        float sw = (float)GetScreenWidth();
        float sh = (float)GetScreenHeight();
        float scx = sw / (float)SCREEN_W;
        float scy = sh / (float)SCREEN_H;
        float sc = (scx < scy) ? scx : scy;
        Rectangle srcR = { 0.0f, 0.0f, (float)SCREEN_W, -(float)SCREEN_H };
        Rectangle dstR = { (sw - SCREEN_W * sc) * 0.5f, (sh - SCREEN_H * sc) * 0.5f,
                           SCREEN_W * sc, SCREEN_H * sc };
        DrawTexturePro(target.texture, srcR, dstR, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
        if (crt) drawCrtOverlay(dstR);
        EndDrawing();
    }

quit:
    UnloadRenderTexture(target);
    if (audioReady) CloseAudioDevice();
    CloseWindow();
    return 0;
}
