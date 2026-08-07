
#include "raylib.h"
#include <stddef.h>

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

typedef struct {
    int size;
    int alive;
    float life;
    float lifeMax;
} FileRec;

typedef enum { SC_TITLE, SC_PLAY, SC_OVER } Screen;

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

    files[f].size = size;
    files[f].alive = 1;
    files[f].lifeMax = 24.0f - playT * 0.15f;
    if (files[f].lifeMax < 10.0f) files[f].lifeMax = 10.0f;
    files[f].life = files[f].lifeMax;

    for (int placed = 0; placed < size; ) {
        int i = (int)(rnd() % GRID_N);
        if (grid[i].kind != EMPTY) continue;
        grid[i].kind = BLOCK;
        grid[i].file = (unsigned char)f;
        grid[i].flash = 0.35f;
        placed++;
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
        score += gained;
        shake = 5.0f + (float)count * 1.5f;
        hitstop = 0.05f + (float)count * 0.012f;
        spawnPop(first % GRID_W, first / GRID_W, gained, FILE_COL[f & 7]);

        int before = assistLevel();
        compacted++;
        if (assistLevel() != before) {
            noticeMsg = (assistLevel() == ASSIST_OFF) ? "TARGET DISPLAY OFF"
                                                      : "TARGET DISPLAY: CARRYING ONLY";
            noticeT = 2.6f;
        }
    }
}

static int targetWindow(int f)
{
    int n = files[f].size;
    int headIdx = headY * GRID_W + headX;
    int best = -1, bestScore = -1;

    for (int s = 0; s + n <= GRID_N; s++) {
        if (s / GRID_W != (s + n - 1) / GRID_W) continue;

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
    combo = 0;
    comboT = 0;
    playT = 0;
    shake = 0;
    moveT = 0;
    headVX = (float)headX;
    headVY = (float)headY;
    hitstop = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) parts[i].life = 0;
    for (int i = 0; i < MAX_POPS; i++) pops[i].life = 0;
    compacted = 0;
    noticeMsg = NULL;
    noticeT = 0;
    writeInterval = 4.5f;
    writeT = 7.0f;

    writeFile(2);
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
            if (grid[i].kind == BLOCK) {
                carrying = grid[i].file;
                grid[i].kind = EMPTY;
                grid[i].flash = 0.2f;
            }
        } else {
            if (grid[i].kind == EMPTY) {
                grid[i].kind = BLOCK;
                grid[i].file = (unsigned char)carrying;
                grid[i].flash = 0.25f;
                spawnBurst(headX, headY, 4, 70.0f, FILE_COL[carrying & 7]);
                carrying = -1;
                checkSorted();
            }
        }
    }

    writeT -= dt;
    if (writeT <= 0.0f) {
        if (!writeFile(rndRange(FILE_MIN, FILE_MAX))) {
            if (score > best) best = score;
            screen = SC_OVER;
            shake = 18.0f;
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

    int ff = focusFile();
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
    DrawRectangleLinesEx((Rectangle){(float)hx - 2, (float)hy - 2, CELL + 4, CELL + 4}, 2, hc);
    if (carrying >= 0)
        DrawRectangle(hx + 9, hy + 9, CELL - 18, CELL - 18, hc);

    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &parts[i];
        if (p->life <= 0.0f) continue;
        float t = p->life / p->maxLife;
        int sz = 2 + (int)(4.0f * t);
        DrawRectangle((int)p->x - sz / 2, (int)p->y - sz / 2, sz, sz,
                      (Color){p->c.r, p->c.g, p->c.b, (unsigned char)(255 * t)});
    }

    for (int i = 0; i < MAX_POPS; i++) {
        Pop *p = &pops[i];
        if (p->life <= 0.0f) continue;
        const char *txt = TextFormat("+%d", p->value);
        int w = MeasureText(txt, 20);
        float t = p->life / 0.9f;
        DrawText(txt, (int)p->x - w / 2, (int)p->y, 20,
                 (Color){p->c.r, p->c.g, p->c.b, (unsigned char)(255 * (t > 1.0f ? 1.0f : t))});
    }

    DrawRectangle(GRID_X - 10, hy + CELL / 2 - 1, GRID_W * CELL + 20, 1,
                  (Color){hc.r, hc.g, hc.b, 40});
}

static void drawHud(void)
{
    DrawText("D E F R A G", 96, 34, 40, TEXTCOL);
    DrawText("1,474,560 BYTES", 96, 80, 10, DIMTEXT);

    DrawText(TextFormat("SCORE %d", score), 420, 40, 20, TEXTCOL);
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

    int col = GRID_X;
    for (int f = 0; f < MAX_FILES; f++) {
        if (!files[f].alive) continue;
        int y = 106;
        float t = (files[f].lifeMax > 0.0f) ? files[f].life / files[f].lifeMax : 0.0f;
        if (t < 0.0f) t = 0.0f;

        for (int k = 0; k < files[f].size; k++)
            DrawRectangle(col + k * 9, y, 7, 12, FILE_COL[f & 7]);

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
    DrawText("MOVE: ARROWS / WASD      PICK & DROP: SPACE      ESC: QUIT",
             GRID_X, GRID_Y + GRID_H * CELL + 44, 10, DIMTEXT);

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

    DrawText("PRESS SPACE TO START", SCREEN_W / 2 - 120, 500, 20, TEXTCOL);
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
    InitWindow(SCREEN_W, SCREEN_H, "DEFRAG");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    rngState = 0x1234567u;
    screen = SC_TITLE;
    resetGame();
#ifdef DEFRAG_START_IN_PLAY
    screen = SC_PLAY;
#endif

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        switch (screen) {
        case SC_TITLE:
            if (actionPressed()) { resetGame(); screen = SC_PLAY; }
            if (IsKeyPressed(KEY_ESCAPE)) goto quit;
            break;
        case SC_PLAY:
            if (hitstop > 0.0f) hitstop -= dt;
            else updatePlay(dt);
            if (IsKeyPressed(KEY_ESCAPE)) screen = SC_TITLE;
            break;
        case SC_OVER:
            if (actionPressed()) { resetGame(); screen = SC_PLAY; }
            if (IsKeyPressed(KEY_ESCAPE)) screen = SC_TITLE;
            if (shake > 0.0f) { shake -= dt * 30.0f; if (shake < 0.0f) shake = 0.0f; }
            break;
        }

        BeginDrawing();
        ClearBackground(BG);

        if (screen == SC_TITLE) {
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

            if (screen == SC_OVER) drawOver();
        }

        EndDrawing();
    }

quit:
    CloseWindow();
    return 0;
}
