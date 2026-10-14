/*
 * duck-soup: a cartoon screensaver of the sight gags from the Marx Brothers'
 * Duck Soup (1933), in the order of the film.
 *
 * His Excellency arrives down a fireman's pole; the lemonade stand and its hat
 * shuffle; the sidecar that goes nowhere; the lemonade stand again; the radio that
 * will not stop; the broken mirror; Paul Revere's ride; the war, in a different
 * uniform every time; and a curtain call. Between them, title cards as in the
 * pictures of the time, and an iris in and out. The music is public-domain band
 * recordings made before 1926; the lemonade stand and the mirror are silent, as in
 * the film, but for a horn.
 *
 *   duck-soup            full screen on every display; any key or mouse movement quits
 *   duck-soup --window   in a window; Esc or Q quits
 *   DuckSoup.scr /s      Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, cut by tools/make_sprites.py) are built into the
 * program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if defined(__APPLE__)
/* Mach-O: its own section names, and C names carry a leading underscore */
#define ART_SECTION ".section __DATA,__const\n"
#define ART_SYM "_art_"
#elif defined(_WIN32)
#define ART_SECTION ".section .rdata,\"dr\"\n"
#define ART_SYM "art_"
#else
#define ART_SECTION ".section .rodata\n"
#define ART_SYM "art_"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".globl " ART_SYM #name "\n" ART_SYM #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".globl " ART_SYM #name "_end\n" ART_SYM #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
/* Every picture, by name and file (src/art.h). The figures are drawn to one scale:
 * Groucho stands 480 high, and the others at their heights beside him, each with
 * its feet at the middle of the bottom edge. */
#include "art.h"
#define X(name, file) ART(name, file)
SPRITES
#undef X
#define X(name, file) SP_##name,
enum { SPRITES NSPRITE };
#undef X
#define X(name, file) {art_##name, art_##name##_end},
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = { SPRITES };
#undef X
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        SDL_SetTextureBlendMode(tex[i], SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;
static int opt_mute = 0;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {10, 9, 12}, INK = {30, 24, 20}, PAPER = {236, 226, 200}, GOLD = {205, 168, 92},
                 RED = {178, 36, 30}, GLASS = {214, 232, 240};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c); vert(x1, y1, c); vert(x2, y2, c); vert(x3, y3, c);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}

/* ---- randomness and timing helpers ---- */
static uint64_t seed_state;
static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double frand(void) { return (next_rand() >> 11) * (1.0 / 9007199254740992.0); }
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }

/* ---- the show's cast ----
 *
 * Everyone and everything on the stage is an actor with a track of keys: at time t
 * it is at x (across the background picture, 0..1), lifted y (in its heights) off
 * its floor, in pose `pose`, facing right unless flipped. A key says how it gets to
 * the next one: it holds still, glides there (eased), slides there (steadily), falls
 * there (faster and faster), or walks or runs there, picking the frames of a cycle as
 * it goes. Each actor has a floor (where its feet are, as part of the picture's
 * height) and a size; a scene sets how tall a 480-high figure is on its picture. */
enum { HOLD, GLIDE, SLIDE, FALL, STRIDE, RUN };
#define GONE (-1)
enum { CY_FIREFLY, CY_FIREFLY_T, CY_CHICO, CY_HARPO, CY_REVERE, CY_CROCKETT, CY_DUCK, NCYCLE };
#define CYCLE(c) (NSPRITE + (c))
static const struct { int first, n; double stride, fps; } CYCLES[NCYCLE] = {
    [CY_FIREFLY] = {SP_f_w1, 4, 0.035, 0},
    [CY_FIREFLY_T] = {SP_f_w1, 4, 0, 7},              /* the same, by time: for walking towards us */
    [CY_CHICO] = {SP_c_w1, 4, 0.028, 0},
    [CY_HARPO] = {SP_h_w1, 4, 0.030, 0},
    [CY_REVERE] = {SP_r_g1, 8, 0, 15},
    [CY_CROCKETT] = {SP_u_r1, 4, 0, 10},
    [CY_DUCK] = {SP_d_b1, 4, 0, 5},
};
typedef struct { double t, x, y; int pose, flip, how; double arc, spin, fl, sz; } Key;   /* fl, sz: a floor and size of its own, or 0 */
typedef struct Actor Actor;
struct Actor {
    Key k[72];
    int n, layer;
    double floor, size, alpha, cut;   /* cut: drawn only above this height on the picture, or 0 */
    Actor *ride0, *ride1;             /* what it rides in or on (the second from ride_at on), or none */
    double ride_at;
    int seat;                         /* RIDER astride it, or PASSENGER down in it */
};
enum { RIDER = 1, PASSENGER };
#define MAXACT 160
static Actor act[MAXACT];
static int nact;

static Actor *actor(int layer, double floor, double size)
{
    Actor *a = &act[nact < MAXACT - 1 ? nact++ : nact];
    memset(a, 0, sizeof *a);
    a->layer = layer; a->floor = floor; a->size = size; a->alpha = 1;
    return a;
}
static Key *key(Actor *a, double t, double x, double y, int pose, int flip, int how)
{
    if (a->n >= (int)(sizeof a->k / sizeof *a->k)) return &a->k[a->n - 1];
    Key *k = &a->k[a->n++];
    *k = (Key){t, x, y, pose, flip, how, 0, 0, 0, 0};
    return k;
}
/* a key's own depth: where its feet are and how big it is there */
static void depth(Key *k, double fl, double sz) { k->fl = fl; k->sz = sz; }
/* the commonest keys: still in a pose, and setting off for the next key's place */
static void at(Actor *a, double t, double x, int pose, int flip) { key(a, t, x, 0, pose, flip, HOLD); }
static void go(Actor *a, double t, double x, int pose, int flip, int how) { key(a, t, x, 0, pose, flip, how); }

typedef struct { int pose, flip; double x, y, spin, pop, fl, sz; } Look;
static Look look(const Actor *a, double t)
{
    Look l = {GONE, 0, 0, 0, 0, 0, a->floor, a->size};
    if (!a->n || t < a->k[0].t) return l;
    int i = 0;
    while (i + 1 < a->n && a->k[i + 1].t <= t) i++;
    const Key *k = &a->k[i];
    l.pose = k->pose; l.flip = k->flip; l.x = k->x; l.y = k->y;
    if (k->fl > 0) { l.fl = k->fl; l.sz = k->sz; }
    if (k->how != HOLD && i + 1 < a->n) {
        const Key *nx = &a->k[i + 1];
        double f = ramp(t, k->t, nx->t);
        double m = k->how == GLIDE ? ease(f) : k->how == FALL ? f * f : f;
        l.x = k->x + (nx->x - k->x) * m;
        if (nx->fl > 0 || k->fl > 0) {                /* moving nearer or further away */
            double f0 = k->fl > 0 ? k->fl : a->floor, s0 = k->fl > 0 ? k->sz : a->size;
            double f1 = nx->fl > 0 ? nx->fl : a->floor, s1 = nx->fl > 0 ? nx->sz : a->size;
            l.fl = f0 + (f1 - f0) * m; l.sz = s0 + (s1 - s0) * m;
        }
        l.y = k->y + (nx->y - k->y) * m + k->arc * 4 * f * (1 - f);
        l.spin = k->spin * 360 * f;
        if (k->pose >= NSPRITE) {
            int c = k->pose - NSPRITE;
            int fr = CYCLES[c].fps > 0 ? (int)((t - k->t) * CYCLES[c].fps)
                                       : (int)(fabs(l.x - k->x) / CYCLES[c].stride);
            l.pose = CYCLES[c].first + fr % CYCLES[c].n;
        }
    } else if (k->pose >= NSPRITE) {
        l.pose = CYCLES[k->pose - NSPRITE].first;
    }
    /* a new pose snaps in with a little squash, as a cartoon does */
    if (k->how == HOLD && (i == 0 || a->k[i - 1].pose != k->pose)) l.pop = exp(-(t - k->t) * 16);
    return l;
}

/* ---- sounds and words ----
 *
 * A horn, glass breaking, a puff, a splat and a match, made here rather than
 * recorded; and the words that pop up with them. Each is put on the show's time-line
 * when the show is made, and mixed into its sound track then. */
enum { FX_HONK, FX_CRASH, FX_PUFF, FX_SPLAT, FX_MATCH, FX_BOOM, FX_SNIP, FX_QUACK, FX_QUACKS, FX_POND, FX_SNORE };
typedef struct { double t; int kind; } Fx;
static Fx fx[96];
static int nfx;
typedef struct { double t0, t1, x, y; const char *say; Rgb c; } Bubble;
static Bubble bubbles[24];
static int nbubble;
static void sound(double t, int kind) { if (nfx < 96) fx[nfx++] = (Fx){t, kind}; }
static void say(double t, double dur, double x, double y, const char *s, Rgb c)
{
    if (nbubble < 24) bubbles[nbubble++] = (Bubble){t, t + dur, x, y, s, c};
}

/* ---- the scenes ---- */
enum { S_OPENER, S_ARRIVAL, S_LEMONADE, S_SIDECAR, S_LEMONADE2, S_RADIO, S_MIRROR, S_RIDE, S_BED, S_WAR, S_FINALE };
#define MAXSCENE 24
enum { M_OPENER, M_ARRIVAL, M_SIDECAR, M_RADIO, M_RIDE, M_WAR, M_FINALE, NMUSIC, M_NONE = -1 };
typedef struct {
    int kind;                  /* which scene it is */
    const char *card;          /* its title card, or none */
    double card_len;           /* how long the card stays */
    int bg;                    /* its picture */
    double tall;               /* how tall a 480-high figure is, as part of the picture's height */
    int music;                 /* the recording that plays under it, from its start */
    double len, music_stop;    /* how long it is, and where its music is cut off (0: plays out) */
    double iris_x, iris_y;     /* where its iris closes, across and down the picture */
    int first, n;              /* its actors */
    int fx0, fx1, b0, b1;      /* its sounds and words */
} Scene;
static Scene scene[MAXSCENE];
static int NSCENE;
#define CARD 2.6                /* how long a title card stays */
static double scene_at[MAXSCENE + 1];   /* when each one's card comes up, and the end */
static double SHOW_LEN, T, anim_t;

/* Where a scene's actors go while it is being made */
static Scene *cur;
static void begin(int kind, const char *card, int bg, double tall, int music)
{
    cur = &scene[NSCENE < MAXSCENE - 1 ? NSCENE++ : NSCENE];
    memset(cur, 0, sizeof *cur);
    cur->kind = kind; cur->card_len = card ? CARD : 0;
    cur->card = card; cur->bg = bg; cur->tall = tall; cur->music = music;
    cur->iris_x = 0.5; cur->iris_y = 0.55;
    cur->first = nact; cur->fx0 = nfx; cur->b0 = nbubble;
}
static void end(double len) { cur->len = len; cur->n = nact - cur->first; cur->fx1 = nfx; cur->b1 = nbubble; }

/* ---- 0. The opening titles ----
 * As the picture's own titles: ducks paddling in a pot on the fire. The curtain goes
 * up on them, the title comes down on its ropes, and the ducks get on with it: a
 * quack, a dive, a drip of sweat as the water warms. Then the curtain comes down. */
#define OPENER_LEN 22.0
static void duck_swim(Actor *d, double t0, double t1, double x0, double x1)
{
    go(d, t0, x0, CYCLE(CY_DUCK), x1 < x0, GLIDE);
    go(d, t1, x1, CYCLE(CY_DUCK), x1 < x0, HOLD);
}
static void build_opener(void)
{
    begin(S_OPENER, NULL, SP_pot, 0.26, M_OPENER);
    /* the water is an oval across the pot: ducks at the back paddle along 0.255,
     * those in front along 0.31, a little bigger */
    Actor *a = actor(2, 0.31, 1.0), *b = actor(2, 0.315, 0.95), *big = actor(1, 0.252, 0.8),
          *little = actor(1, 0.252, 0.8), *top = actor(1, 0.247, 0.78), *boat = actor(2, 0.30, 0.95);
    /* A, in front, the quacker */
    duck_swim(a, 0, 3.2, 0.44, 0.52);
    at(a, 3.4, 0.52, SP_d_up, 0);                   /* the title comes down */
    duck_swim(a, 4.6, 6.8, 0.52, 0.58);
    at(a, 7.0, 0.58, SP_d_quack, 0); sound(7.05, FX_QUACKS);
    duck_swim(a, 8.4, 11.5, 0.58, 0.47);
    at(a, 12.0, 0.47, SP_d_sweat, 1);
    duck_swim(a, 13.6, 16.2, 0.47, 0.55);
    at(a, 16.4, 0.55, SP_d_quack, 0); sound(16.45, FX_QUACK);
    duck_swim(a, 17.2, OPENER_LEN, 0.55, 0.6);
    /* B, in front, the diver */
    duck_swim(b, 0, 3.2, 0.40, 0.38);
    at(b, 3.4, 0.38, SP_d_up, 1);
    duck_swim(b, 4.6, 8.6, 0.38, 0.40);
    at(b, 9.0, 0.40, SP_d_dive, 0); sound(9.0, FX_SPLAT);
    at(b, 9.4, 0.40, SP_d_tail, 0);
    at(b, 11.2, 0.40, SP_d_pop, 0); sound(11.2, FX_SPLAT);
    at(b, 11.9, 0.40, SP_d_shake, 0);
    at(b, 12.8, 0.40, SP_d_back, 0);
    duck_swim(b, 14.0, OPENER_LEN, 0.40, 0.37);
    /* at the back, the big one and her duckling behind her */
    for (int i = 0; i < 2; i++) {
        Actor *d = i ? little : big;
        int pose = i ? SP_d_little : SP_d_big;
        double lag = i * 0.5, off = i ? -0.075 : 0;
        go(d, 0, 0.42 + off, pose, 0, GLIDE); go(d, 6.0 + lag, 0.6 + off * -1 + (i ? 0 : 0), pose, 1, GLIDE);
        go(d, 6.6 + lag, 0.6 - off, pose, 1, GLIDE); go(d, 14.0 + lag, 0.42 - off, pose, 0, GLIDE);
        go(d, 14.6 + lag, 0.42 + off, pose, 0, GLIDE); go(d, OPENER_LEN, 0.55 + off, pose, 0, HOLD);
    }
    sound(5.6, FX_QUACK); sound(14.8, FX_QUACK);
    /* and the two in hats, gliding past each other with dignity */
    go(top, 0, 0.62, SP_d_tophat, 1, GLIDE); go(top, 10.0, 0.40, SP_d_tophat, 1, GLIDE);
    go(top, 10.8, 0.40, SP_d_tophat, 0, GLIDE); go(top, OPENER_LEN, 0.56, SP_d_tophat, 0, HOLD);
    go(boat, 0, 0.60, SP_d_boater, 1, GLIDE); go(boat, 7.5, 0.62, SP_d_boater, 1, GLIDE);
    go(boat, 8.0, 0.62, SP_d_boater, 1, GLIDE); go(boat, 15.5, 0.52, SP_d_boater, 1, GLIDE);
    go(boat, 16.0, 0.52, SP_d_boater, 0, GLIDE); go(boat, OPENER_LEN, 0.6, SP_d_boater, 0, HOLD);
    sound(10.2, FX_QUACK); sound(19.0, FX_QUACK); sound(20.3, FX_QUACK);
    sound(1.0, FX_POND);
    cur->music_stop = OPENER_LEN;
    end(OPENER_LEN);
}

/* ---- 1. His Excellency arrives ----
 * The guards blow their trumpets and the ballerinas raise their swords, and
 * everyone looks to the staircase for the new leader. He arrives instead down the
 * fireman's pole, and joins them in looking for himself. */
static void build_arrival(void)
{
    begin(S_ARRIVAL, "HIS EXCELLENCY ARRIVES", SP_reception_hall, 0.36, M_ARRIVAL);
    /* the guard down each side, further back the nearer the middle */
    static const double GX[] = {0.09, 0.18, 0.27, 0.73, 0.82, 0.91}, GF[] = {0.86, 0.83, 0.80, 0.80, 0.83, 0.86};
    for (int i = 0; i < 6; i++) {
        Actor *g = actor(2 + (i == 0 || i == 5), GF[i], 0.62 + (GF[i] - 0.8) * 3);
        int flip = i >= 3;
        double off = 0.12 * (i % 3);
        at(g, 0, GX[i], SP_gd_stand, flip);
        for (double t = 2.0 + off; t < 9; t += 1.2) { at(g, t, GX[i], SP_gd_raise, flip); at(g, t + 0.3, GX[i], SP_gd_blow, flip); }
        at(g, 9.2, GX[i], SP_gd_stand, flip);
        for (double t = 20.0 + off; t < 24; t += 1.2) { at(g, t, GX[i], SP_gd_raise, flip); at(g, t + 0.3, GX[i], SP_gd_blow, flip); }
        at(g, 24.2, GX[i], SP_gd_lower, flip);
        at(g, 28.6, GX[i], SP_gd_salute, flip);
        at(g, 31.5, GX[i], SP_gd_stand, flip);
    }
    for (int i = 0; i < 2; i++) {                   /* the ballerinas, swords up over the aisle */
        Actor *b = actor(1, 0.765, 0.6);
        double x = i ? 0.565 : 0.435;
        at(b, 0, x, SP_bl_sword, i);
        at(b, 9.4, x, SP_bl_toes, i);
        at(b, 12.5, x, SP_bl_sword, i);
        at(b, 28.6, x, SP_bl_curtsy, i);
        at(b, 31.0, x, SP_bl_bow, i);
        at(b, 33.0, x, SP_bl_sword, i);
    }
    Actor *drum = actor(4, 0.98, 0.95), *page = actor(4, 0.98, 0.95);
    for (double t = 0; t < 36; t += 0.45) {
        at(drum, t, 0.06, SP_dr_hit, 0); at(drum, t + 0.16, 0.06, SP_dr_stand, 0);
        at(page, t, 0.94, ((int)(t / 0.9) & 1) ? SP_pg_wave : SP_pg_stand, 1);
    }
    /* His Excellency, down the pole at the back of the hall and then down the aisle */
    const double BACK = 0.745, BACK_SZ = 0.55, FRONT = 0.93, FRONT_SZ = 1.2;
    Actor *f = actor(3, FRONT, FRONT_SZ);
    at(f, 0, 0.5, GONE, 0);
    depth(key(f, 13.0, 0.5, 1.9, SP_f_pole, 0, FALL), BACK, BACK_SZ);
    depth(key(f, 14.4, 0.5, 0.0, SP_f_land, 0, HOLD), BACK, BACK_SZ);
    sound(14.4, FX_PUFF);
    Actor *puff = actor(1, BACK + 0.01, 0.45);
    at(puff, 0, 0.5, GONE, 0); at(puff, 14.4, 0.5, SP_p_puff, 0); at(puff, 15.0, 0.5, GONE, 0);
    depth(key(f, 15.2, 0.5, 0, SP_f_stand, 0, HOLD), BACK, BACK_SZ);
    depth(key(f, 16.0, 0.5, 0, SP_f_brows, 0, HOLD), BACK, BACK_SZ);
    depth(key(f, 17.0, 0.5, 0, CYCLE(CY_FIREFLY_T), 1, SLIDE), BACK, BACK_SZ);
    at(f, 19.6, 0.44, SP_f_lean, 0);               /* he waits for himself, with everyone */
    at(f, 21.6, 0.44, SP_f_point, 1);
    at(f, 23.0, 0.44, SP_f_brows, 0);
    at(f, 24.4, 0.44, SP_f_ash, 0);
    go(f, 25.6, 0.44, CYCLE(CY_FIREFLY), 1, STRIDE);
    at(f, 27.2, 0.32, SP_f_bow, 0);
    at(f, 29.8, 0.32, SP_f_stand, 0);
    go(f, 30.4, 0.32, CYCLE(CY_FIREFLY), 0, STRIDE);
    at(f, 34.4, 1.25, GONE, 0);
    end(36.5);
    cur->iris_x = 0.5; cur->iris_y = 0.6;
}

/* ---- 2. The lemonade stand ----
 * The two spies sidle up to the lemonade seller, and the hats go round: his, theirs,
 * his again, faster and faster, till he is left bareheaded. A horn in his face, a
 * leg in his hand, and they are off. No music: there is none in the film. */
static Actor *vendor_a, *chico_a, *harpo_a;
static void hat_flight(double t, double x0, double y0, double x1, double y1, int hat, double turns)
{
    Actor *h = actor(4, 0.86, 0.32);
    at(h, 0, x0, GONE, 0);
    Key *k = key(h, t, x0, y0, hat, 0, GLIDE);
    k->arc = 0.9; k->spin = turns;
    key(h, t + 0.32, x1, y1, GONE, 0, HOLD);
}
static void build_lemonade(void)
{
    begin(S_LEMONADE, "THE LEMONADE STAND", SP_street, 0.40, M_NONE);
    Actor *v = vendor_a = actor(2, 0.88, 1.0), *c = chico_a = actor(2, 0.88, 1.0), *h = harpo_a = actor(2, 0.885, 1.0);
    const double VX = 0.66, CX = 0.42, HX = 0.53;
    at(v, 0, VX, SP_v_wipe, 1);
    at(v, 3.0, VX, SP_v_glare, 1);
    at(c, 0, -0.15, GONE, 0); go(c, 1.6, -0.12, CYCLE(CY_CHICO), 0, STRIDE); at(c, 4.4, CX, SP_c_hands, 0);
    at(h, 0, -0.3, GONE, 0); go(h, 2.0, -0.28, CYCLE(CY_HARPO), 0, STRIDE); at(h, 5.0, HX, SP_h_tip, 0);
    at(c, 5.4, CX, SP_c_whisper, 0);
    at(v, 5.6, VX, SP_v_arms, 1);
    /* the hats go round, faster and faster */
    double t = 7.0, dt = 0.62;
    for (int i = 0; i < 9; i++, t += dt, dt = fmax(0.3, dt * 0.88)) {
        if (i & 1) {
            at(v, t, VX, SP_v_slam, 1); at(c, t, CX, SP_c_twohats, 0);
            hat_flight(t, CX, 1.0, VX, 1.0, SP_p_boater, 1);
        } else {
            at(v, t, VX, i % 4 ? SP_v_twohats : SP_v_grab, 1); at(c, t, CX, SP_c_hat_on, 0);
            hat_flight(t, VX, 1.0, CX, 1.0, SP_p_boater, -1);
        }
        at(h, t, HX, (i % 3 == 2) ? SP_h_tip : SP_h_bye, 0);
        if (i % 3 == 2) hat_flight(t + dt * 0.4, HX, 1.0, VX, 1.0, SP_p_tophat, 1);
    }
    at(v, t, VX, SP_v_twohats, 1); at(c, t, CX, SP_c_laugh, 0); at(h, t, HX, SP_h_tip, 0);
    at(v, t + 1.4, VX, SP_v_rage, 1);
    t += 3.0;
    at(h, t, HX + 0.03, SP_h_honk, 0); sound(t + 0.1, FX_HONK); say(t + 0.1, 1.0, HX + 0.09, 1.05, "HONK!", RED);
    at(v, t + 0.9, VX, SP_v_fist, 1);
    at(h, t + 1.2, HX, SP_h_tip, 0);
    t += 2.2;
    at(h, t, HX + 0.02, SP_h_leg, 0);             /* a leg in his hand */
    at(v, t + 0.15, VX, SP_v_leg, 1);
    at(c, t + 0.6, CX, SP_c_laugh, 0);
    t += 2.6;
    at(h, t, HX, SP_h_tip, 0); at(v, t, VX, SP_v_glare, 1);
    t += 1.0;
    go(v, t, VX, SP_v_chase, 1, GLIDE); at(v, t + 0.8, 0.5, SP_v_trip, 1);
    go(c, t + 0.1, CX, CYCLE(CY_CHICO), 1, STRIDE); at(c, t + 2.2, -0.25, GONE, 0);
    go(h, t + 0.05, HX, CYCLE(CY_HARPO), 1, STRIDE); at(h, t + 2.3, -0.3, GONE, 0);
    at(v, t + 2.6, 0.5, SP_v_burn, 1);
    end(t + 6.0);
    cur->iris_x = 0.5; cur->iris_y = 0.62;
}

/* ---- 3. The sidecar ----
 * Short, between every two scenes: the two of them ride in from
 * the left together and stop; then off goes the motorcycle and the sidecar stays,
 * or off goes the sidecar and the motorcycle stays, or the sidecar goes by on its
 * own with a horn. A running gag. */
static void build_sidecar_bit(int which)
{
    begin(S_SIDECAR, NULL, SP_garden, 0.40, M_SIDECAR);
    const double MX = 0.47;
    /* back to front: the motorcycle (with the sidecar on it, or not), its rider astride
     * it, the sidecar on its own (it is on our side of the motorcycle), the passenger
     * down in the sidecar, and the exhaust. The two of them ride: they go where the
     * motorcycle and the sidecar go, and sit in their seats. */
    Actor *moto = actor(1, 0.88, 1.45), *side = actor(3, 0.88, 1.45);
    Actor *gr = actor(which == 1 ? 2 : 4, 0.86, 1.0), *hr = actor(which == 1 ? 4 : 2, 0.86, 1.0);
    Actor *puff = actor(5, 0.88, 0.7);
    at(puff, 0, MX, GONE, 0);
    if (which == 2) {                               /* the sidecar by itself, honking */
        at(moto, 0, 0, GONE, 0); at(gr, 0, 0, GONE, 0);
        go(side, 0.4, 1.35, SP_p_sidecar, 1, SLIDE); at(side, 3.6, -0.4, GONE, 0);
        hr->ride0 = side; hr->seat = PASSENGER; hr->layer = 4;
        at(hr, 0, 0, SP_h_ride, 1);
        sound(1.9, FX_HONK); say(1.9, 1.0, 0.5, 1.2, "HONK!", RED);
        cur->music_stop = 4.6;
        end(4.6);
        cur->iris_x = 0.2; cur->iris_y = 0.66;
        return;
    }
    Actor *ra = which ? gr : hr, *pa = which ? hr : gr;
    ra->ride0 = moto; ra->seat = RIDER;
    pa->ride0 = moto; pa->ride1 = side; pa->ride_at = 3.2; pa->seat = PASSENGER;
    at(ra, 0, 0, which ? SP_f_sit : SP_h_ride, 0);
    at(pa, 0, 0, which ? SP_h_ride : SP_f_sit, 0);
    at(side, 0, MX, GONE, 0);
    go(moto, 0.2, -0.45, SP_p_motoside, 0, GLIDE); at(moto, 2.0, MX, SP_p_motoside, 0);
    at(puff, 2.8, MX - 0.27, SP_p_puff, 1); at(puff, 3.3, MX - 0.27, GONE, 0); sound(2.8, FX_PUFF);
    at(moto, 3.2, MX, SP_p_moto, 0); at(side, 3.2, MX - 0.035, SP_p_sidecar, 0);
    if (which == 0) {                               /* off goes the motorcycle */
        go(moto, 3.4, MX, SP_p_moto, 0, FALL); at(moto, 4.6, 1.4, GONE, 0);
        at(gr, 4.8, 0, SP_f_situp, 0);
        at(gr, 6.4, 0, SP_f_sit, 0);
    } else {                                        /* off goes the sidecar */
        go(side, 3.4, MX - 0.035, SP_p_sidecar, 0, FALL); at(side, 4.6, 1.4, GONE, 0);
        at(gr, 4.8, 0, SP_f_situp, 0);
        at(gr, 6.4, 0, SP_f_sit, 0);
    }
    cur->music_stop = 8.2;
    end(8.2);
    cur->iris_x = which ? MX : MX - 0.08; cur->iris_y = 0.6;
}

/* ---- 4. The lemonade stand again ----
 * Harpo paddles in the lemonade, and sets the seller's hat alight. */
static void build_lemonade2(void)
{
    begin(S_LEMONADE2, "THE LEMONADE STAND AGAIN", SP_street, 0.40, M_NONE);
    const double VX = 0.67, TX = 0.5;
    Actor *v = actor(2, 0.88, 1.0), *h = actor(2, 0.885, 1.0), *tub = actor(3, 0.9, 0.62);
    at(tub, 0, TX, SP_p_tub, 0);
    at(v, 0, VX, SP_v_wipe, 1);
    at(h, 0, -0.3, GONE, 0); go(h, 1.0, -0.28, CYCLE(CY_HARPO), 0, STRIDE);
    at(h, 3.6, 0.42, SP_h_tip, 0);
    at(h, 4.4, 0.42, SP_h_foot, 0);
    Key *k = key(h, 5.6, 0.42, 0, SP_h_foot, 0, GLIDE); k->arc = 0.35;
    key(h, 6.2, TX, 0.1, SP_h_wade, 0, HOLD);
    at(v, 7.0, VX, SP_v_glare, 1);
    at(v, 8.2, VX, SP_v_tap, 1);
    at(v, 10.2, VX, SP_v_rage, 1);
    k = key(h, 11.6, TX, 0.1, SP_h_wade, 0, GLIDE); k->arc = 0.35;
    at(h, 12.2, 0.40, SP_h_strike, 0); sound(12.6, FX_MATCH);
    at(h, 13.2, 0.40, SP_h_match, 0);
    at(v, 13.0, VX, SP_v_arms, 1);
    go(h, 14.0, 0.40, CYCLE(CY_HARPO), 0, STRIDE);
    at(h, 15.4, VX - 0.075, SP_h_match, 0);
    at(v, 16.0, VX, SP_v_fire1, 1); sound(16.0, FX_MATCH);
    go(h, 16.6, VX - 0.075, CYCLE(CY_HARPO), 1, STRIDE); at(h, 19.4, -0.3, GONE, 0);
    at(v, 16.9, VX, SP_v_fire2, 1);
    k = key(v, 18.6, VX, 0, SP_v_chase, 1, GLIDE); k->arc = 0.5;
    key(v, 19.4, TX, 0.08, SP_v_wet, 1, HOLD); sound(19.4, FX_SPLAT);
    key(v, 22.4, TX, 0.08, SP_v_burn, 1, HOLD);
    end(25.5);
    cur->iris_x = 0.5; cur->iris_y = 0.6;
}

/* ---- 5. The radio ----
 * In Mrs Teasdale's front room at night the radio will not stop playing. Harpo
 * cuts its wire; it plays on. He honks at it; it plays on. He backs away from it,
 * straight into the big mirror, and that is the end of the mirror. */
static double radio_crash;
static void build_radio(void)
{
    begin(S_RADIO, "THE RADIO", SP_mr_room, 0.48, M_RADIO);
    /* x here is along the Mirror Scene's path through the room: 0.5 is the frame */
    Actor *radio = actor(2, 0, 0.75), *h = actor(2, 0, 1.0);
    for (double t = 0; t < 11.0; t += 0.16) at(radio, t, 0.14, ((int)(t / 0.16) & 1) ? SP_p_radio2 : SP_p_radio, 0);
    radio_crash = 11.0;
    at(radio, radio_crash, 0.14, SP_p_radio, 0);
    at(h, 0, -0.5, GONE, 0); go(h, 1.2, -0.45, CYCLE(CY_HARPO), 0, STRIDE);
    at(h, 3.4, 0.26, SP_h_tip, 1);
    at(h, 4.2, 0.24, SP_h_snip, 1); sound(4.6, FX_SNIP);
    at(h, 5.4, 0.26, SP_h_tip, 0);
    at(h, 6.4, 0.25, SP_h_honk, 1); sound(6.5, FX_HONK);
    at(h, 7.6, 0.26, SP_h_smitten, 0);
    go(h, 8.6, 0.26, CYCLE(CY_HARPO), 1, STRIDE);  /* backing away from it */
    at(h, radio_crash, 0.5, SP_h_leg, 0);
    sound(radio_crash, FX_CRASH);
    say(radio_crash + 0.05, 1.3, 0.5, 1.15, "CRASH!", RED);
    at(h, radio_crash + 0.6, 0.53, SP_h_tip, 0);
    go(h, radio_crash + 1.6, 0.53, CYCLE(CY_HARPO), 0, STRIDE); at(h, radio_crash + 3.6, 1.3, GONE, 0);
    cur->music_stop = radio_crash;
    end(radio_crash + 4.2);
    cur->iris_x = 0.6; cur->iris_y = 0.5;
}

/* ---- 6. The mirror ----
 * The broken-mirror gag, as in the Mirror Scene screensaver: one man in a nightshirt
 * on the near side of the empty frame, and his "reflection" on the other, who is
 * really Harpo, copying him a moment later. */
#define MWALK (-2)      /* a mirror pose: walking, from this key's place to the next one's */
#define MIRROR (-3)     /* an H pose: be G's reflection */
#define LAG 0.07        /* how far behind G his reflection usually is */
#define NEAR 0.40       /* where G stands by the frame, along his path; H at 1 - NEAR */
#define OFF (-0.4)      /* off the left edge */
typedef struct { double t, x; int pose, flip; double lag; } MKey;
typedef struct { MKey k[160]; int n; } MTrack;
static MTrack G, Hk, Ck;
enum { ON_SCREEN, ON_G, ON_H };
typedef struct { double t0, t1, x0, y0, x1, y1, lift, turns; int on0, on1; } Flight;
static Flight hats[8];
static int nhat;
/* The path he walks, in the room picture's width and height: where it is at 0 (the
 * near room) and 1 (the room beyond), with the middle of the frame at 0.5. TALL is
 * his height at 0, as part of the picture's height, and FAR_SCALE his size at 1. */
#define PATH_AX 0.05
#define PATH_AY 0.80
#define PATH_BX 1.05
#define PATH_BY 0.69
#define TALL 0.48
#define FAR_SCALE 0.55
static void mkey(MTrack *tr, double t, double x, int pose, int flip, double lag)
{
    if (tr->n < (int)(sizeof tr->k / sizeof *tr->k)) tr->k[tr->n++] = (MKey){t, x, pose, flip, lag};
}
static double now, gx;
static int gflip;
static void g(int pose, double dur) { mkey(&G, now, gx, pose, gflip, 0); now += dur; }
static void g_walk(double to, double dur)
{
    gflip = to < gx;
    mkey(&G, now, gx, MWALK, gflip, 0);
    gx = to; now += dur;
}
static void h_mirror(double t, double lag) { mkey(&Hk, t, 0, MIRROR, 0, lag); }
static void h(double t, int pose) { mkey(&Hk, t, 1 - NEAR, pose, 0, 0); }
static void fly(double t0, double t1, int on0, double x0, double y0, int on1, double x1, double y1, double lift, double turns)
{
    if (nhat < 8) hats[nhat++] = (Flight){t0, t1, x0, y0, x1, y1, lift, turns, on0, on1};
}
static void build_mirror(void)
{
    begin(S_MIRROR, "THE MIRROR", SP_mr_room, 0.48, M_NONE);
    G.n = Hk.n = Ck.n = nhat = 0;
    now = 0.4; gx = OFF; gflip = 0;
    mkey(&Ck, 0, 0, GONE, 0, 0);
    h_mirror(0, 0);
    mkey(&G, 0, gx, SP_mr_g_stand, 0, 0);
    /* in he creeps, and finds himself in the frame */
    g_walk(NEAR, 2.8);
    gflip = 0;
    g(SP_mr_g_stand, 0.9); g(SP_mr_g_peer, 1.5); g(SP_mr_g_brows, 1.2);
    /* trying to catch him out: a duck, a sudden turn */
    g(SP_mr_g_crouch, 0.5); g(SP_mr_g_stand, 0.35); g(SP_mr_g_crouch, 0.3); g(SP_mr_g_peer, 0.9);
    h_mirror(now, 0.35); g(SP_mr_g_spin, 0.22); gflip = 1; g(SP_mr_g_stand, 0.6); g(SP_mr_g_spin, 0.22); gflip = 0; g(SP_mr_g_brows, 1.1);
    h_mirror(now, LAG);
    /* off the frame and back: the reflection goes too */
    g_walk(OFF, 1.7);
    gflip = 0; g(SP_mr_g_stand, 1.6);
    gx = 0.02; g(SP_mr_g_peer, 1.3);
    g_walk(NEAR, 1.2); gflip = 0; g(SP_mr_g_stand, 0.6);
    /* the dance: six moves in a new order every time, and once he is late */
    int moves[] = {SP_mr_g_arms, SP_mr_g_kick, SP_mr_g_wave, SP_mr_g_crouch, SP_mr_g_cigar, SP_mr_g_peer, SP_mr_g_brows, SP_mr_g_spin};
    int nm = sizeof moves / sizeof *moves;
    for (int i = nm - 1; i > 0; i--) { int j = (int)(frand() * (i + 1)) % (i + 1), k = moves[i]; moves[i] = moves[j]; moves[j] = k; }
    int late = 2 + (int)(frand() * 4) % 4;
    for (int i = 0; i < 6; i++) {
        if (i == late) h_mirror(now, 0.4);
        g(moves[i], 0.55 + 0.35 * frand());
        if (moves[i] == SP_mr_g_spin) { gflip = 0; }
        if (i == late) { h_mirror(now, LAG); g(SP_mr_g_stand, 0.25); g(SP_mr_g_brows, 0.9); g(SP_mr_g_peer, 0.9); }
    }
    g(SP_mr_g_stand, 0.6);
    /* he drops his hat: G has none to drop. G picks one up anyway, and so on. Where
     * the hat is held in the drawings, in his heights from his middle and the floor: */
    const double DROP_X = 0.2, DROP_Y = 0.44, HOLD_X = 0.34, HOLD_Y = 0.42;
    h(now, SP_mr_h_drop);
    g(SP_mr_g_brows, 0.45);
    h(now, SP_mr_h_stand);
    double down = now + 0.4;
    fly(now, down, ON_H, DROP_X, DROP_Y, ON_H, DROP_X, 0.0, 0.05, 0.4);
    g(SP_mr_g_brows, 0.5);
    g(SP_mr_g_peer, 1.1);
    h_mirror(now, LAG);
    fly(down, now + 0.45 + LAG, ON_H, DROP_X, 0.0, ON_H, DROP_X, 0.0, 0, 0);   /* it lies there till he is down for it */
    g(SP_mr_g_crouch, 0.6); g(SP_mr_g_hold, 1.0); g(SP_mr_g_hat_on, 1.1); g(SP_mr_g_grin, 1.2); g(SP_mr_g_hold, 0.5);
    fly(now, now + 0.9, ON_G, HOLD_X, HOLD_Y, ON_SCREEN, -0.5, 1.4, 0.6, -1.5);
    fly(now + LAG, now + 0.9 + LAG, ON_H, HOLD_X, HOLD_Y, ON_SCREEN, 1.5, 1.4, 0.6, 1.5);
    g(SP_mr_g_stand, 0.8);
    /* the horn */
    g(SP_mr_g_stand, 1.0);
    h(now, SP_mr_h_horn);
    sound(now + 0.1, FX_HONK);
    say(now + 0.1, 1.0, 1 - NEAR, 1.0, "HONK!", RED);
    g(SP_mr_g_stand, 0.9);
    h(now, SP_mr_h_grin);
    g(SP_mr_g_peer, 1.3);
    h_mirror(now, LAG);
    g(SP_mr_g_brows, 0.8);
    /* through the frame and back, each passing the other */
    g_walk(1 - NEAR, 1.6); gflip = 1; g(SP_mr_g_stand, 0.6); g(SP_mr_g_brows, 0.9);
    g_walk(NEAR, 1.6); gflip = 0; g(SP_mr_g_stand, 0.6);
    /* and a third man walks in, between them */
    mkey(&Ck, now, 1.5, MWALK, 0, 0);
    g(SP_mr_g_stand, 1.8); g(SP_mr_g_peer, 1.4);
    mkey(&Ck, now, 0.52, SP_mr_c_stand, 0, 0);
    g(SP_mr_g_brows, 0.8);
    mkey(&Ck, now, 0.52, SP_mr_c_shrug, 0, 0);
    g(SP_mr_g_brows, 1.6);
    mkey(&Ck, now, 0.52, SP_mr_c_stand, 0, 0);
    g_walk(OFF - 0.02, 2.2);
    mkey(&Ck, now, 0.52, SP_mr_c_shrug, 0, 0);
    g(SP_mr_g_stand, 2.4);
    mkey(&G, now + 30, gx, SP_mr_g_stand, gflip, 0);
    mkey(&Ck, now + 30, 0.52, SP_mr_c_shrug, 0, 0);
    end(now);
    cur->iris_x = 0.6; cur->iris_y = 0.5;
}
typedef struct { int pose; double x; int flip; } MPose;
static int mfind(const MTrack *tr, double t)
{
    int i = 0;
    while (i + 1 < tr->n && tr->k[i + 1].t <= t) i++;
    return i;
}
static MPose mat(const MTrack *tr, double t, const int *cycle, int ncycle, double stride)
{
    int i = mfind(tr, t);
    const MKey *k = &tr->k[i];
    MPose p = {k->pose, k->x, k->flip};
    if (k->pose == MWALK) {
        const MKey *nx = i + 1 < tr->n ? &tr->k[i + 1] : k;
        double f = nx->t > k->t ? ramp(t, k->t, nx->t) : 1;
        p.x = k->x + (nx->x - k->x) * f;
        p.pose = cycle[(int)(fabs(p.x - k->x) / stride) % ncycle];
        p.flip = nx->x < k->x;
    }
    return p;
}
static const int G_WALK[4] = {SP_mr_g_w1, SP_mr_g_w2, SP_mr_g_w3, SP_mr_g_w4};
static const int C_WALK[2] = {SP_mr_c_w1, SP_mr_c_w2};
static MPose g_at(double t) { return mat(&G, t, G_WALK, 4, 0.045); }
static MPose h_at(double t)
{
    const MKey *k = &Hk.k[mfind(&Hk, t)];
    if (k->pose != MIRROR) return (MPose){k->pose, k->x, 0};
    MPose p = g_at(t - k->lag);
    p.x = 1 - p.x; p.flip = !p.flip;
    if (p.pose >= SP_mr_g_arms && p.pose <= SP_mr_g_wave) p.pose += SP_mr_hp_arms - SP_mr_g_arms;   /* it is Harpo, in the same disguise */
    return p;
}
/* his own four drawings, and the third man's, face left */
static int faces_left(int pose) { return (pose >= SP_mr_h_drop && pose <= SP_mr_h_stand) || (pose >= SP_mr_c_shrug && pose <= SP_mr_c_w2); }

/* ---- 7. Paul Revere's ride ----
 * Through the town at a gallop, until a lit window and a pretty face stop him. */
static void build_ride(void)
{
    begin(S_RIDE, "THE RIDE", SP_street_night, 0.30, M_RIDE);
    Actor *r = actor(2, 0.93, 1.0), *dust = actor(3, 0.94, 0.7);
    const double WX = 0.475;                      /* below the lit window he stops at */
    at(dust, 0, 0, GONE, 0);
    at(r, 0, -0.3, GONE, 0);
    go(r, 1.0, -0.3, CYCLE(CY_REVERE), 0, SLIDE); at(r, 3.8, 1.3, GONE, 0);
    go(r, 5.0, 1.3, CYCLE(CY_REVERE), 1, GLIDE); at(r, 7.2, WX, SP_r_stop, 1);
    at(dust, 7.2, WX + 0.04, SP_p_puff, 1); at(dust, 7.8, WX, GONE, 0);
    at(r, 8.0, WX, SP_r_look, 1);
    at(r, 9.8, WX, SP_r_kiss, 1);
    say(10.0, 1.6, WX + 0.02, 1.35, "+", RED);
    at(r, 11.4, WX, SP_r_off, 1);
    at(r, 12.2, WX, SP_hs_stand, 1);
    at(r, 14.0, WX, SP_hs_bored, 1);
    at(r, 15.6, WX, SP_hs_eat, 1);
    at(r, 18.2, WX, SP_hs_stand, 1);
    at(r, 19.2, WX, SP_r_look, 1);
    at(dust, 20.0, WX, SP_p_puff, 0); at(dust, 20.5, WX, GONE, 0);
    go(r, 20.0, WX, CYCLE(CY_REVERE), 0, SLIDE); at(r, 21.8, 1.3, GONE, 0);
    go(r, 23.4, -0.3, CYCLE(CY_REVERE), 0, GLIDE); at(r, 25.8, 0.8, SP_r_stop, 0);
    at(r, 26.6, 0.8, SP_r_look, 0);
    at(r, 27.6, 0.8, SP_r_kiss, 0);
    end(30.0);
    cur->iris_x = 0.8; cur->iris_y = 0.7;
}
/* the window he stops at, and when its light is on: across and down the picture */
static const double WINDOW[4] = {0.455, 0.345, 0.495, 0.46};
static int window_lit(double t) { return t < 12.6 || t > 18.0; }

/* ---- 7b. In bed with the horse ----
 * Harpo turns in for the night, and so does his horse, in the same bed: they sleep,
 * the horse snores, they fight over the quilt, he honks at it, and it kisses him,
 * and they cuddle up and go back to sleep. Close up, by the moonlit window. */
static void build_bed(void)
{
    begin(S_BED, "GOOD NIGHT", SP_bedroom, 0.64, M_NONE);
    Actor *b = actor(2, 1.0, 1.0);
    double t = 0;
    static const int BREATH[] = {SP_b_sleep, SP_b_in, SP_b_sleep, SP_b_out};
    for (int i = 0; t < 6.0; i++, t += 0.75) {
        at(b, t, 0.5, BREATH[i % 4], 0);
        if (i % 4 == 1) say(t, 1.4, 0.6 - 0.02 * (i % 3), 0.62 + 0.04 * (i % 2), "Z", PAPER);
    }
    at(b, 3.0, 0.5, SP_b_snore, 0); sound(3.0, FX_SNORE);
    at(b, 4.6, 0.5, SP_b_sleep, 0);
    at(b, 6.0, 0.5, SP_b_sit, 0);
    at(b, 7.2, 0.5, SP_b_pat, 0);
    at(b, 8.4, 0.5, SP_b_yawn, 0);
    at(b, 9.6, 0.5, SP_b_pull, 0);
    at(b, 10.6, 0.5, SP_b_pullback, 0);
    at(b, 11.4, 0.5, SP_b_tug, 0);
    at(b, 12.0, 0.5, SP_b_pull, 0);
    at(b, 12.5, 0.5, SP_b_pullback, 0);
    at(b, 13.0, 0.5, SP_b_tug, 0);
    at(b, 14.2, 0.5, SP_b_honk, 0); sound(14.3, FX_HONK); say(14.3, 1.0, 0.62, 0.66, "HONK!", RED);
    at(b, 15.6, 0.5, SP_b_offended, 0);
    at(b, 17.2, 0.5, SP_b_kiss, 0); say(17.3, 1.6, 0.5, 0.62, "+", RED);
    at(b, 19.0, 0.5, SP_b_cuddle, 0);
    for (t = 20.5; t < 25.5; t += 1.6) say(t, 1.4, 0.52, 0.62, "Z", PAPER);
    sound(21.0, FX_SNORE);
    end(26.0);
    cur->iris_x = 0.5; cur->iris_y = 0.7;
}

/* ---- 8. The war ----
 * Groucho fights it in a new uniform every time we look. */
static void build_war(void)
{
    begin(S_WAR, "THE WAR", SP_battlefield, 0.42, M_WAR);
    Actor *f = actor(2, 0.92, 1.0), *puff = actor(3, 0.92, 1.1), *boom = actor(1, 0.62, 1.4);
    const double FX_ = 0.3;
    static const int COSTUME[] = {SP_u_union, SP_u_confed, SP_u_redcoat, SP_u_scout, SP_u_crockett};
    at(puff, 0, FX_, GONE, 0); at(boom, 0, 0.75, GONE, 0);
    double t = 1.0;
    for (int i = 0; i < 5; i++, t += 2.8) {
        at(f, t, FX_, COSTUME[i], 0);
        if (i) { at(puff, t - 0.12, FX_, SP_p_puff, i & 1); at(puff, t + 0.35, FX_, GONE, 0); sound(t - 0.12, FX_PUFF); }
    }
    go(f, t, FX_, CYCLE(CY_CROCKETT), 0, SLIDE); at(f, t + 1.8, 0.62, SP_u_crockett, 0);
    go(f, t + 2.2, 0.62, CYCLE(CY_CROCKETT), 1, SLIDE); at(f, t + 4.0, FX_, SP_u_crockett, 0);
    t += 4.6;
    at(puff, t - 0.12, FX_, SP_p_puff, 0); at(puff, t + 0.35, FX_, GONE, 0); sound(t - 0.12, FX_PUFF);
    at(f, t, FX_, SP_u_napoleon, 0); t += 2.8;
    at(puff, t - 0.12, FX_, SP_p_puff, 1); at(puff, t + 0.35, FX_, GONE, 0); sound(t - 0.12, FX_PUFF);
    at(f, t, FX_, SP_u_ww1, 0);
    at(boom, t + 1.0, 0.72, SP_p_puff, 0); at(boom, t + 1.8, 0.72, GONE, 0); sound(t + 1.0, FX_BOOM);
    at(f, t + 1.05, FX_, SP_u_duck, 0);
    at(f, t + 2.6, FX_ + 0.03, SP_u_binoc, 0);
    t += 5.4;
    at(puff, t - 0.12, FX_, SP_p_puff, 0); at(puff, t + 0.35, FX_, GONE, 0); sound(t - 0.12, FX_PUFF);
    at(f, t, FX_, SP_u_general, 0); t += 2.6;
    at(f, t, FX_, SP_u_salute, 0); t += 1.8;
    at(f, t, FX_, SP_u_grin, 0); t += 2.0;
    end(t + 0.6);
    cur->iris_x = 0.3; cur->iris_y = 0.55;
}

/* ---- 9. The curtain call ----
 * Back in the hall they all take a bow, and the audience lets them have it. */
static void tomato(double t, double x0, double x1, double y1, int who_floor)
{
    (void)who_floor;
    Actor *a = actor(4, 0.86, 0.22);
    at(a, 0, x0, GONE, 0);
    Key *k = key(a, t, x0, -0.6, SP_p_tomato, 0, SLIDE);
    k->arc = 1.6; k->spin = 1.5;
    key(a, t + 0.9, x1, y1, SP_p_splat, 0, HOLD);
    key(a, t + 1.6, x1, y1, GONE, 0, HOLD);
    sound(t + 0.9, FX_SPLAT);
}
static void build_finale(void)
{
    begin(S_FINALE, "THE CURTAIN CALL", SP_reception_hall, 0.38, M_FINALE);
    Actor *c = actor(2, 0.9, 1.0), *f = actor(2, 0.9, 1.0), *h = actor(2, 0.9, 1.0), *v = actor(2, 0.9, 1.0);
    at(c, 0, 0.24, SP_c_hands, 0); at(f, 0, 0.42, SP_f_stand, 0); at(h, 0, 0.58, SP_h_tip, 1); at(v, 0, 0.76, SP_v_arms, 1);
    at(c, 1.6, 0.24, SP_c_tip, 0); at(f, 1.6, 0.42, SP_f_bow, 0); at(h, 1.6, 0.58, SP_h_tip, 1); at(v, 1.6, 0.76, SP_v_glare, 1);
    at(c, 3.6, 0.24, SP_c_hands, 0); at(f, 3.6, 0.42, SP_f_brows, 0); at(h, 3.6, 0.58, SP_h_honk, 1); sound(3.7, FX_HONK);
    tomato(4.4, 0.6, 0.76, 0.75, 0); at(v, 5.3, 0.76, SP_v_rage, 1);
    tomato(6.2, 0.3, 0.24, 0.75, 0); at(c, 7.1, 0.24, SP_c_shrug, 0);
    tomato(7.8, 0.5, 0.42, 0.8, 0); at(f, 8.7, 0.42, SP_f_huff, 0);
    at(h, 9.0, 0.58, SP_h_smitten, 1);
    at(f, 10.6, 0.42, SP_f_ash, 0);
    at(f, 12.4, 0.42, SP_f_brows, 0);
    at(c, 12.4, 0.24, SP_c_laugh, 0); at(h, 12.4, 0.58, SP_h_tip, 0); at(v, 12.4, 0.76, SP_v_burn, 1);
    end(19.5);
    cur->iris_x = 0.42; cur->iris_y = 0.45;
}

/* ---- the show: all the scenes, one after the other ---- */
static void build_show(void)
{
    nact = nfx = nbubble = NSCENE = 0;
    build_opener();
    void (*const order[])(void) = {build_arrival, build_lemonade, build_lemonade2, build_radio,
                                   build_mirror, build_ride, build_bed, build_war, build_finale};
    int n = sizeof order / sizeof *order;
    for (int i = 0; i < n; i++) {
        order[i]();
        if (i + 1 < n) build_sidecar_bit(i % 3);
    }
    double t = 0;
    for (int s = 0; s < NSCENE; s++) {
        scene_at[s] = t;
        for (int i = scene[s].fx0; i < scene[s].fx1; i++) fx[i].t += t + scene[s].card_len;   /* their sounds, in show time */
        t += scene[s].card_len + scene[s].len;
    }
    scene_at[NSCENE] = t;
    SHOW_LEN = t + 3.0;                            /* and THE END */
}
/* which scene T is in, and how far into it (negative while its card is up) */
static int scene_of(double t, double *local)
{
    int s = 0;
    while (s + 1 < NSCENE && scene_at[s + 1] <= t) s++;
    *local = t - scene_at[s] - scene[s].card_len;
    return s;
}

/* ---- the sound track ----
 *
 * The recordings are built in as Ogg Vorbis. When the show is made, its whole sound
 * track is mixed at once: each scene's music from where the scene begins (cut off
 * where the scene says), and the horns, crashes and puffs made here. The show then
 * follows the sound's own clock while it plays. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
#undef L
#undef R
#undef C
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
ART(m_opener, "m_opener.ogg") ART(m_arrival, "m_arrival.ogg") ART(m_sidecar, "m_sidecar.ogg") ART(m_radio, "m_radio.ogg")
ART(m_ride, "m_ride.ogg") ART(m_war, "m_war.ogg") ART(m_finale, "m_finale.ogg")
static const unsigned char *MUSIC_FILE[NMUSIC][2] = {
    {art_m_opener, art_m_opener_end}, {art_m_arrival, art_m_arrival_end}, {art_m_sidecar, art_m_sidecar_end}, {art_m_radio, art_m_radio_end},
    {art_m_ride, art_m_ride_end}, {art_m_war, art_m_war_end}, {art_m_finale, art_m_finale_end},
};
/* the recorded noises (tools/make_sounds.py): a pond of ducks, a run of quacks,
 * single quacks, and a rubber bulb horn */
ART(s_pond, "s_pond.ogg") ART(s_quacks, "s_quacks.ogg") ART(s_quack1, "s_quack1.ogg") ART(s_quack2, "s_quack2.ogg")
ART(s_quack3, "s_quack3.ogg") ART(s_quack4, "s_quack4.ogg") ART(s_quack5, "s_quack5.ogg")
ART(s_honk1, "s_honk1.ogg") ART(s_honk2, "s_honk2.ogg") ART(s_honk3, "s_honk3.ogg")
enum { SM_POND, SM_QUACKS, SM_QUACK1, SM_QUACK5 = SM_QUACK1 + 4, SM_HONK1, SM_HONK3 = SM_HONK1 + 2, NSAMPLE };
static const unsigned char *SAMPLE_FILE[NSAMPLE][2] = {
    {art_s_pond, art_s_pond_end}, {art_s_quacks, art_s_quacks_end},
    {art_s_quack1, art_s_quack1_end}, {art_s_quack2, art_s_quack2_end}, {art_s_quack3, art_s_quack3_end},
    {art_s_quack4, art_s_quack4_end}, {art_s_quack5, art_s_quack5_end},
    {art_s_honk1, art_s_honk1_end}, {art_s_honk2, art_s_honk2_end}, {art_s_honk3, art_s_honk3_end},
};
static short *sample[NSAMPLE];
static int sample_len[NSAMPLE];
#define RATE 22050
static short *music[NMUSIC];
static int music_len[NMUSIC];
static short *track;                    /* the show's sound track, mono */
static int track_len;
static volatile int pcm_pos;            /* where the sound has got to, in samples */
static SDL_AudioDeviceID audio;

static void load_music(void)
{
    for (int m = 0; m < NMUSIC; m++) {
        int ch, rate;
        short *pcm;
        int n = stb_vorbis_decode_memory(MUSIC_FILE[m][0], (int)(MUSIC_FILE[m][1] - MUSIC_FILE[m][0]), &ch, &rate, &pcm);
        if (n <= 0) continue;
        music[m] = malloc(sizeof *music[m] * n);
        for (int i = 0; i < n; i++) music[m][i] = pcm[i * ch];   /* they are mono at 22050 already */
        music_len[m] = n;
        free(pcm);
    }
    for (int m = 0; m < NSAMPLE; m++) {
        int ch, rate;
        short *pcm;
        int n = stb_vorbis_decode_memory(SAMPLE_FILE[m][0], (int)(SAMPLE_FILE[m][1] - SAMPLE_FILE[m][0]), &ch, &rate, &pcm);
        if (n <= 0) continue;
        sample[m] = malloc(sizeof *sample[m] * n);
        for (int i = 0; i < n; i++) sample[m][i] = pcm[i * ch];
        sample_len[m] = n;
        free(pcm);
    }
}
static void add_sample(float *buf, int len, int at, int m, double gain)
{
    if (!sample[m]) return;
    for (int i = 0; i < sample_len[m] && at + i < len; i++) if (at + i >= 0) buf[at + i] += (float)(sample[m][i] / 32768.0 * gain);
}
/* the noises, added into a float buffer at sample i */
static double noise(void) { return frand() * 2 - 1; }
static void mix_fx(float *buf, int len, int at, int kind)
{
    double lp = 0;
    switch (kind) {
    case FX_HONK:                                  /* the bulb horn: one honk, now and then two */
        add_sample(buf, len, at, frand() < 0.25 ? SM_HONK3 : SM_HONK1 + (int)(frand() * 2), 0.9);
        break;
    case FX_SNORE:                                 /* a horse's snore: a long low rattle, and a whistle out */
        for (int i = 0; i < RATE * 1.6 && at + i < len; i++) {
            double t = (double)i / RATE, in = t < 0.9;
            double env = in ? sin(M_PI * t / 0.9) : sin(M_PI * (t - 0.9) / 0.7) * 0.5;
            lp += (noise() - lp) * (in ? 0.05 : 0.3);
            double rattle = in ? (0.6 + 0.4 * sin(2 * M_PI * 28 * t)) : 1;
            double whistle = in ? 0 : 0.25 * sin(2 * M_PI * (900 - 300 * (t - 0.9)) * t);
            buf[at + i] += (float)(env * (lp * 2.2 * rattle + whistle) * 0.5);
        }
        break;
    case FX_QUACK:
        add_sample(buf, len, at, SM_QUACK1 + (int)(frand() * 5) % 5, 0.75);
        break;
    case FX_QUACKS:
        add_sample(buf, len, at, SM_QUACKS, 0.75);
        break;
    case FX_POND:
        add_sample(buf, len, at, SM_POND, 0.35);
        break;
    case FX_CRASH:                                 /* glass: a burst, and then the pieces tinkling down */
        for (int i = 0; i < RATE * 1.6 && at + i < len; i++) {
            double t = (double)i / RATE, n = noise();
            double v = (n - lp) * exp(-t * 7) * 0.8;
            lp = n;
            buf[at + i] += (float)v;
        }
        for (int k = 0; k < 26; k++) {
            int s = at + (int)(RATE * (0.05 + frand() * 1.1));
            double f = 2200 + frand() * 3800, a = 0.08 + frand() * 0.1;
            for (int i = 0; i < RATE * 0.25 && s + i < len; i++)
                buf[s + i] += (float)(a * sin(2 * M_PI * f * i / RATE) * exp(-(double)i / RATE * 22));
        }
        break;
    case FX_PUFF: case FX_BOOM: {                  /* a soft whoosh; a boom is bigger and lower */
        double d = kind == FX_BOOM ? 1.2 : 0.35, cut = kind == FX_BOOM ? 0.04 : 0.15, a = kind == FX_BOOM ? 1.4 : 0.5;
        for (int i = 0; i < RATE * d && at + i < len; i++) {
            double t = (double)i / RATE;
            lp += (noise() - lp) * cut;
            buf[at + i] += (float)(a * lp * fmin(1, t / 0.02) * exp(-t * 4.5 / d));
        }
        break; }
    case FX_SPLAT:                                 /* a wet thump */
        for (int i = 0; i < RATE * 0.25 && at + i < len; i++) {
            double t = (double)i / RATE;
            lp += (noise() - lp) * 0.25;
            buf[at + i] += (float)((0.6 * lp + 0.4 * sin(2 * M_PI * 110 * t)) * exp(-t * 18));
        }
        break;
    case FX_MATCH:                                 /* a scratch and a flare */
        for (int i = 0; i < RATE * 0.5 && at + i < len; i++) {
            double t = (double)i / RATE, n = noise();
            buf[at + i] += (float)((n - lp) * 0.25 * (t < 0.08 ? 1 : exp(-(t - 0.08) * 6) * 0.4));
            lp = n;
        }
        break;
    case FX_SNIP:                                  /* scissors: two quick clicks */
        for (int c = 0; c < 2; c++)
            for (int i = 0; i < RATE * 0.03 && at + i + c * 2400 < len; i++)
                buf[at + c * 2400 + i] += (float)(noise() * 0.4 * exp(-(double)i / RATE * 160));
        break;
    }
}
static void make_track(void)
{
    int len = (int)(SHOW_LEN * RATE) + RATE;
    float *buf = calloc(len, sizeof *buf);
    for (int s = 0; s < NSCENE; s++) {
        int m = scene[s].music;
        if (m < 0 || !music[m]) continue;
        int at = (int)((scene_at[s] + scene[s].card_len - (scene[s].card ? 0.3 : 0)) * RATE);
        int n = music_len[m];
        if (scene[s].music_stop > 0) n = (int)fmin(n, (scene[s].music_stop + 0.3) * RATE);
        int fade = scene[s].music_stop > 0 ? RATE / 40 : 0;
        for (int i = 0; i < n && at + i < len; i++)
            buf[at + i] += music[m][i] / 32768.0f * 0.85f * (float)(fade && i > n - fade ? (double)(n - i) / fade : 1);
    }
    for (int i = 0; i < nfx; i++) mix_fx(buf, len, (int)(fx[i].t * RATE), fx[i].kind);
    short *t = malloc(sizeof *t * len);
    for (int i = 0; i < len; i++) t[i] = (short)(32767 * tanh(buf[i]));   /* a soft limit */
    free(buf);
    if (audio) SDL_LockAudioDevice(audio);
    free(track);
    track = t; track_len = len; pcm_pos = 0;
    if (audio) SDL_UnlockAudioDevice(audio);
}
static void play(void *u, Uint8 *out, int bytes)
{
    (void)u;
    short *o = (short *)out;
    int n = bytes / (int)sizeof(short);
    for (int i = 0; i < n; i++, pcm_pos++) o[i] = track && pcm_pos < track_len ? track[pcm_pos] : 0;
}
static void start_sound(void)
{
    if (opt_mute || opt_snapshot || audio) return;
    SDL_AudioSpec want = {0}, got;
    want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 1;
    want.samples = 1024; want.callback = play;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
}

static void new_show(void)
{
    build_show();
    make_track();
    T = 0;
}
static void update(double dt)
{
    anim_t += dt;
    if (audio) T = (double)pcm_pos / RATE;
    else T += dt * opt_speed;
    if (T >= SHOW_LEN) new_show();
}

/* ---- drawing ---- */
static double unit;
static int sw, sh;
static double rx0, ry0, rw, rh;       /* where the current scene's picture is on the screen */

/* a picture covering the screen, cut to fit, and pushed in a little over `push` */
static void backdrop(int which, double push)
{
    double s = fmax((double)sw / sprite[which].w, (double)sh / sprite[which].h) * (1 + 0.04 * push);
    rw = sprite[which].w * s; rh = sprite[which].h * s;
    rx0 = (sw - rw) / 2; ry0 = (sh - rh) / 2 - rh * 0.012 * push;
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_FRect d = {(float)rx0, (float)ry0, (float)rw, (float)rh};
    SDL_RenderCopyF(ren, cur_tex[which], NULL, &d);
}
/* one frame of a figure, feet at (x, y), drawn `tall` high for a 480-high figure */
static double cut_y;                  /* figures are drawn only above this, when it is set */
static void figure(int which, double x, double y, double tall, int flip, double alpha, double sx, double sy, double spin)
{
    flush(ren);
    if (which < 0 || which >= NSPRITE || alpha <= 0.01 || !cur_tex || !cur_tex[which]) return;
    double k = tall / 480.0, w = sprite[which].w * k * sx, hgt = sprite[which].h * k * sy;
    SDL_FRect dst = {(float)(x - w / 2), (float)(y - hgt + 10 * k * sy), (float)w, (float)hgt};
    SDL_Rect src = {0, 0, sprite[which].w, sprite[which].h};
    if (cut_y > 0 && dst.y + dst.h > cut_y) {        /* down in a sidecar: only the top of him */
        double keep = (cut_y - dst.y) / dst.h;
        if (keep <= 0) return;
        src.h = (int)(sprite[which].h * keep); dst.h = (float)(dst.h * keep);
    }
    SDL_FPoint mid = {(float)(w / 2), (float)(hgt / 2)};
    SDL_SetTextureAlphaMod(cur_tex[which], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, spin, &mid, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureAlphaMod(cur_tex[which], 255);
}
static void shadow(double x, double y, double r, double a)
{
    int n = 28;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(x, y, col(INK, 0.3 * a));
    for (int i = 0; i < n; i++) vert(x + r * cos(i * 2 * M_PI / n), y + r * 0.2 * sin(i * 2 * M_PI / n), col(INK, 0));
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
static int is_prop(int pose) { return pose >= SP_p_boater && pose <= SP_p_tyrol; }
/* an actor of the current scene, at scene time t */
/* Where a seat is in a vehicle's picture (across and down, facing right), and where
 * a rider's hips are in his: so that he sits in it, not by it */
static int seat_at(int vehicle, int seat, double *fx, double *fy, double *rim)
{
    *rim = 0;
    if (vehicle == SP_p_motoside) {
        if (seat == RIDER) { *fx = 0.47; *fy = 0.36; } else { *fx = 0.27; *fy = 0.60; *rim = 0.47; }
    } else if (vehicle == SP_p_moto && seat == RIDER) { *fx = 0.42; *fy = 0.36; }
    else if (vehicle == SP_p_sidecar && seat == PASSENGER) { *fx = 0.30; *fy = 0.60; *rim = 0.47; }
    else return 0;
    return 1;
}
static void hips(int pose, double *hx, double *hy)
{
    if (pose == SP_f_sit) { *hx = 0.36; *hy = 0.80; }
    else if (pose == SP_f_situp) { *hx = 0.40; *hy = 0.80; }
    else if (pose == SP_h_ride) { *hx = 0.30; *hy = 0.70; }
    else { *hx = 0.5; *hy = 0.55; }
}
static void draw_rider(const Actor *a, double t, double tall)
{
    const Actor *v = a->ride1 && t >= a->ride_at ? a->ride1 : a->ride0;
    Look vl = look(v, t), l = look(a, t);
    if (vl.pose < 0 || l.pose < 0) return;
    double fx, fy, rim, hx, hy;
    if (!seat_at(vl.pose, a->seat, &fx, &fy, &rim)) return;
    if (vl.flip) fx = 1 - fx;
    double vsize = tall * vl.sz, k = vsize / 480, vw = sprite[vl.pose].w * k, vh = sprite[vl.pose].h * k;
    double vx = rx0 + rw * vl.x, top = ry0 + rh * vl.fl - vl.y * vsize + 10 * k - vh;
    double sx = vx - vw / 2 + fx * vw, sy = top + fy * vh;
    hips(l.pose, &hx, &hy);
    if (l.flip) hx = 1 - hx;
    double rs = tall * l.sz, rk = rs / 480, pw = sprite[l.pose].w * rk, ph = sprite[l.pose].h * rk;
    cut_y = rim > 0 ? top + rim * vh : 0;
    figure(l.pose, sx - hx * pw + pw / 2, sy - hy * ph + ph - 10 * rk, rs, l.flip, a->alpha, 1, 1, 0);
    cut_y = 0;
}
static void draw_actor(const Actor *a, double t, double tall)
{
    if (a->ride0) { draw_rider(a, t, tall); return; }
    Look l = look(a, t);
    if (l.pose < 0) return;
    double x = rx0 + rw * l.x, feet = ry0 + rh * l.fl, size = tall * l.sz;
    double breathe = is_prop(l.pose) ? 0 : 0.006 * sin(anim_t * 2.6 + (a - act) * 1.7);
    double sy = 1 - 0.07 * l.pop + breathe, sx = 1 + 0.05 * l.pop;
    if (!is_prop(l.pose) || l.pose == SP_p_tub || l.pose == SP_p_moto || l.pose == SP_p_motoside || l.pose == SP_p_sidecar)
        if (!a->cut) shadow(x, feet, size * 0.22, 1 - fmin(l.y, 1));
    cut_y = a->cut > 0 ? ry0 + rh * a->cut : 0;
    figure(l.pose, x, feet - l.y * size, size, l.flip, a->alpha, sx, sy, l.spin);
    cut_y = 0;
}
static void draw_bubbles(int s, double t, double tall, int path)
{
    for (int i = scene[s].b0; i < scene[s].b1; i++) {
        const Bubble *b = &bubbles[i];
        if (t < b->t0 || t > b->t1) continue;
        double q = ramp(t, b->t0, b->t1), pop = 1 + 0.3 * sin(fmin(q * 8, 1) * M_PI);
        double x, y;
        if (path) {                                   /* along the mirror room's path */
            x = rx0 + rw * (PATH_AX + (PATH_BX - PATH_AX) * b->x);
            double feet = ry0 + rh * (PATH_AY + (PATH_BY - PATH_AY) * b->x), ht = rh * TALL * (1 + (FAR_SCALE - 1) * b->x);
            y = feet - b->y * ht;
        } else { x = rx0 + rw * b->x; y = ry0 + rh * 0.9 - b->y * tall; }
        if (b->say[0] == '+') {                       /* a heart */
            double r = unit * 1.6 * pop, yy = y - q * unit * 8;
            SDL_Color c = col(b->c, 1 - ramp(q, 0.6, 1));
            disc(x - r * 0.5, yy, r * 0.62, c, c); disc(x + r * 0.5, yy, r * 0.62, c, c);
            vb_need(3, 3); int k = nv;
            vert(x - r * 1.08, yy + r * 0.2, c); vert(x + r * 1.08, yy + r * 0.2, c); vert(x, yy + r * 1.5, c);
            ib[ni++] = k; ib[ni++] = k + 1; ib[ni++] = k + 2;
            continue;
        }
        text(b->say, x, y - q * unit * 3, unit * 1.0 * pop, 1.15, col(b->c, 1 - ramp(q, 0.7, 1)));
    }
    flush(ren);
}

/* ---- the mirror room ---- */
static void spot(double x, double *sx, double *feet, double *tall)
{
    *sx = rx0 + rw * (PATH_AX + (PATH_BX - PATH_AX) * x);
    *feet = ry0 + rh * (PATH_AY + (PATH_BY - PATH_AY) * x);
    *tall = rh * TALL * (1 + (FAR_SCALE - 1) * x);
}
static void person(MPose p)
{
    if (p.pose < 0) return;
    double x, feet, tall;
    spot(p.x, &x, &feet, &tall);
    if (x < -tall || x > sw + tall) return;
    shadow(x, feet, tall * 0.2, 1);
    figure(p.pose, x, feet, tall, p.flip, 1, 1, 1, 0);
}
/* the opening of the mirror, in the room picture: its four corners */
static const double OPENING[4][2] = {{0.52, 0.2649}, {0.70, 0.2247}, {0.70, 0.7642}, {0.52, 0.7022}};
static void mirror_glass(double a)
{
    double p[4][2];
    for (int i = 0; i < 4; i++) { p[i][0] = rx0 + rw * OPENING[i][0]; p[i][1] = ry0 + rh * OPENING[i][1]; }
    quad(p[0][0], p[0][1], p[1][0], p[1][1], p[2][0], p[2][1], p[3][0], p[3][1], col(GLASS, 0.42 * a));
    for (int i = 0; i < 3; i++) {                 /* streaks of light across it */
        double u0 = 0.15 + i * 0.28, u1 = u0 + 0.1;
        double xa = p[0][0] + (p[1][0] - p[0][0]) * u0, ya = p[0][1] + (p[1][1] - p[0][1]) * u0;
        double xb = p[0][0] + (p[1][0] - p[0][0]) * u1, yb = p[0][1] + (p[1][1] - p[0][1]) * u1;
        double dx = (p[3][0] - p[0][0]) * 0.55, dy = (p[3][1] - p[0][1]) * 0.55;
        quad(xa, ya, xb, yb, xb + dx * 0.8, yb + dy * 0.8, xa + dx * 0.8, ya + dy * 0.8, col(PAPER, 0.18 * a));
    }
    flush(ren);
}
/* the pieces of the mirror flying, `q` seconds after it broke */
static void shards(double q)
{
    if (q < 0 || q > 2.2) return;
    uint64_t keep = seed_state;
    seed_state = 0x5EEDULL;
    double cx = rx0 + rw * 0.61, cy = ry0 + rh * 0.5;
    for (int i = 0; i < 46; i++) {
        double a = frand() * 2 * M_PI, v = (0.2 + frand()) * rh * 0.55, s = (0.01 + frand() * 0.03) * rh;
        double x = cx + cos(a) * v * q, y = cy + sin(a) * v * q * 0.6 + 0.9 * rh * q * q, turn = frand() * 9 * q;
        double fade = 1 - ramp(q, 1.4, 2.2);
        quad(x + cos(turn) * s, y + sin(turn) * s, x + cos(turn + 2.2) * s * 0.7, y + sin(turn + 2.2) * s * 0.7,
             x + cos(turn + 3.6) * s, y + sin(turn + 3.6) * s, x + cos(turn + 5) * s * 0.5, y + sin(turn + 5) * s * 0.5, col(GLASS, 0.85 * fade));
    }
    seed_state = keep;
    if (q < 0.15) rect(0, 0, sw, sh, col(PAPER, 0.6 * (1 - q / 0.15)));
    flush(ren);
}
/* the near room, with the opening cut out of it, over whoever is beyond the frame */
static void pic_part_room(void)
{
    flush(ren);
    if (!cur_tex || !cur_tex[SP_mr_room_front]) return;
    SDL_FRect d = {(float)rx0, (float)ry0, (float)rw, (float)rh};
    SDL_RenderCopyF(ren, cur_tex[SP_mr_room_front], NULL, &d);
}
static void draw_mirror_room(int s, double t)
{
    backdrop(SP_mr_room, ramp(t, 0, scene[s].len));
    double flicker = 0.5 + 0.3 * sin(anim_t * 7.1) * sin(anim_t * 2.3 + 1);
    rect(0, 0, sw, sh, (SDL_Color){255, 190, 110, (Uint8)(10 + 8 * flicker)});
    flush(ren);
    if (scene[s].kind == S_RADIO) {
        /* night, the radio, and Harpo backing into the glass */
        double tall = rh * TALL;
        if (t < radio_crash) mirror_glass(1);
        for (int side = 1; side >= 0; side--) {
            for (int i = scene[s].first; i < scene[s].first + scene[s].n; i++) {
                Look l = look(&act[i], t);
                if (l.pose < 0 || (l.x >= 0.5) != side) continue;
                double x, feet, ht;
                spot(l.x, &x, &feet, &ht);
                ht *= act[i].size;
                shadow(x, feet, ht * 0.2, 1);
                double jig = (l.pose == SP_p_radio2) ? sin(anim_t * 60) * unit * 0.25 : 0;
                figure(l.pose, x + jig, feet, ht, l.flip, 1, 1 + 0.05 * l.pop, 1 - 0.07 * l.pop, jig * 0.6);
            }
            if (side) pic_part_room();
        }
        (void)tall;
        rect(0, 0, sw, sh, (SDL_Color){20, 30, 70, 70});
        flush(ren);
        shards(t - radio_crash);
        return;
    }
    /* the mirror gag: everyone beyond the frame first, then the near room over them
     * with the opening cut out, then everyone on the near side */
    MPose gp = g_at(t), hp = h_at(t), cp = mat(&Ck, t, C_WALK, 2, 0.06);
    for (int side = 1; side >= 0; side--) {
        if (cp.pose >= 0 && (cp.x >= 0.5) == side) person(cp);
        flush(ren);
        for (int i = 0; i < nhat; i++) {
            const Flight *f = &hats[i];
            if (t < f->t0 || t > f->t1) continue;
            double q = ramp(t, f->t0, f->t1), xs[2], ys[2], ts[2], ps[2];
            for (int e = 0; e < 2; e++) {
                int on = e ? f->on1 : f->on0;
                double x = e ? f->x1 : f->x0, y = e ? f->y1 : f->y0, feet;
                if (on == ON_SCREEN) { ps[e] = x; spot(x, &xs[e], &feet, &ts[e]); }
                else {
                    MPose p = on == ON_G ? gp : hp;
                    int left = faces_left(p.pose) != p.flip;
                    ps[e] = p.x;
                    spot(p.x, &xs[e], &feet, &ts[e]);
                    xs[e] += (left ? -x : x) * ts[e];
                }
                ys[e] = feet - y * ts[e];
            }
            if ((ps[0] + (ps[1] - ps[0]) * q >= 0.5) != side) continue;
            double y = ys[0] + (ys[1] - ys[0]) * q * q - f->lift * ts[0] * 4 * q * (1 - q);
            double tl = ts[0] + (ts[1] - ts[0]) * q, hw = tl * 0.3, hh = hw * sprite[SP_mr_hat].h / sprite[SP_mr_hat].w;
            flush(ren);
            if (cur_tex && cur_tex[SP_mr_hat]) {
                SDL_FRect d = {(float)(xs[0] + (xs[1] - xs[0]) * q - hw / 2), (float)(y - hh), (float)hw, (float)hh};
                SDL_RenderCopyExF(ren, cur_tex[SP_mr_hat], NULL, &d, f->turns * 360 * q, NULL, SDL_FLIP_NONE);
            }
        }
        if (hp.pose >= 0 && (hp.x >= 0.5) == side) person(hp);
        flush(ren);
        if ((gp.x >= 0.5) == side) person(gp);
        flush(ren);
        if (side) pic_part_room();
    }
}

/* ---- title cards and the iris ---- */
static void border(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    line(x0, y0, x1, y0, w, c); line(x1, y0, x1, y1, w, c); line(x1, y1, x0, y1, w, c); line(x0, y1, x0, y0, w, c);
}
static void title_card(const char *title, double a)
{
    rect(0, 0, sw, sh, col(BG, a));
    double m = unit * 5, s = unit * 1.3;
    SDL_Color gold = col(GOLD, a), paper = col(PAPER, a);
    border(m, m, sw - m, sh - m, unit * 0.35, gold);
    border(m + unit * 1.2, m + unit * 1.2, sw - m - unit * 1.2, sh - m - unit * 1.2, unit * 0.15, gold);
    for (int i = 0; i < 4; i++) {                  /* a little flourish in each corner */
        double cx = i & 1 ? sw - m - unit * 3 : m + unit * 3, cy = i & 2 ? sh - m - unit * 3 : m + unit * 3;
        disc(cx, cy, unit * 0.7, gold, gold);
    }
    while (text_width(title, s) > sw * 0.8) s *= 0.9;
    text(title, sw / 2.0, sh * 0.5 - 3 * s, s, 1.0, paper);
    line(sw * 0.38, sh * 0.5 + 5 * s, sw * 0.62, sh * 0.5 + 5 * s, unit * 0.2, gold);
    flush(ren);
}
/* black all round a circle of radius r at (cx, cy): the iris of an old film camera */
static void iris(double cx, double cy, double r)
{
    double R = hypot(sw, sh) * 1.2;
    if (r >= hypot(fmax(cx, sw - cx), fmax(cy, sh - cy))) return;
    int n = 72;
    for (int i = 0; i < n; i++) {
        double a = i * 2 * M_PI / n, b = (i + 1) * 2 * M_PI / n;
        quad(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r,
             cx + cos(b) * R, cy + sin(b) * R, cx + cos(a) * R, cy + sin(a) * R, col(BG, 1));
    }
    flush(ren);
}
/* the flicker and grain of an old print */
static void old_film(void)
{
    double f = 0.03 + 0.02 * sin(anim_t * 37) * sin(anim_t * 13);
    rect(0, 0, sw, sh, col(BG, f));
    uint64_t keep = seed_state;
    seed_state = 1 + (uint64_t)(anim_t * 24);
    for (int i = 0; i < 3; i++) {                  /* a scratch or two */
        if (frand() < 0.5) continue;
        double x = frand() * sw;
        line(x, 0, x + (frand() - 0.5) * unit, sh, unit * 0.08, col(PAPER, 0.12));
    }
    for (int i = 0; i < 40; i++) {                 /* dust */
        double x = frand() * sw, y = frand() * sh, r = unit * (0.05 + frand() * 0.12);
        disc(x, y, r, col(frand() < 0.5 ? INK : PAPER, 0.25), col(INK, 0));
    }
    seed_state = keep;
    flush(ren);
}

/* ---- the curtain and the title ---- */
static const Rgb VELVET = {150, 22, 30}, VELVET_DARK = {70, 8, 14};
static void quadc(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
/* One half of a stage curtain, hanging from the top, `width` wide from its outer
 * edge; as it is drawn back its folds bunch up. */
static void curtain_half(int left, double width)
{
    int folds = 9;
    double fw = width / folds, sway = sin(anim_t * 1.3 + left) * unit * 0.3;
    for (int i = 0; i < folds; i++) {
        double xa = left ? i * fw : sw - (i + 1) * fw, xb = xa + fw, xm = (xa + xb) / 2;
        double bx = (left ? 1 : -1) * sway * i / folds;
        SDL_Color dark = col(VELVET_DARK, 1), lit = col(VELVET, 1);
        quadc(xa, 0, xm, 0, xm + bx, sh, xa + bx, sh, dark, lit, lit, dark);
        quadc(xm, 0, xb, 0, xb + bx, sh, xm + bx, sh, lit, dark, dark, lit);
    }
    /* the gold fringe along its foot */
    double x0 = left ? 0 : sw - width;
    rect(x0, sh - unit * 1.6, width, unit * 1.6, col(GOLD, 1));
    for (double x = x0; x < x0 + width; x += unit * 0.7) line(x, sh - unit * 1.6, x, sh - unit * 0.2, unit * 0.18, col((Rgb){150, 110, 40}, 1));
    flush(ren);
}
static void curtain(double open)          /* 0 closed, 1 drawn right back */
{
    double half = sw / 2.0 * (1 - 0.86 * ease(open)) + unit;
    curtain_half(1, half);
    curtain_half(0, half);
    /* the pelmet across the top, with its scallops and tassels */
    double ph = sh * 0.11;
    rect(0, 0, sw, ph, col(VELVET_DARK, 1));
    for (double x = 0; x < sw + unit * 6; x += unit * 6) disc(x + unit * 3, ph, unit * 3.1, col(VELVET_DARK, 1), col(VELVET_DARK, 1));
    rect(0, ph - unit * 0.6, sw, unit * 0.6, col(GOLD, 1));
    for (double x = 0; x < sw + unit * 6; x += unit * 6) disc(x + unit * 3, ph + unit * 3.4, unit * 0.55, col(GOLD, 1), col(GOLD, 1));
    flush(ren);
}
/* the title on a card, let down on two ropes; `drop` 0 is up out of sight, 1 is down */
static void placard(double drop, double bounce)
{
    double w = sw * 0.46, h = sh * 0.3, cx = sw / 2.0;
    double y = -h - unit * 4 + (sh * 0.6 + h + unit * 4) * drop + bounce;   /* it hangs in front of the fire, below the ducks */
    SDL_Color rope = col((Rgb){120, 90, 50}, 1), gold = col(GOLD, 1), ink = col(INK, 1);
    line(cx - w * 0.3, 0, cx - w * 0.3, y, unit * 0.35, rope);
    line(cx + w * 0.3, 0, cx + w * 0.3, y, unit * 0.35, rope);
    rect(cx - w / 2 + unit * 0.6, y + unit * 0.8, w, h, col(INK, 0.5));          /* its shadow */
    rect(cx - w / 2, y, w, h, col(PAPER, 1));
    border(cx - w / 2 + unit, y + unit, cx + w / 2 - unit, y + h - unit, unit * 0.5, gold);
    border(cx - w / 2 + unit * 2, y + unit * 2, cx + w / 2 - unit * 2, y + h - unit * 2, unit * 0.18, ink);
    for (int i = 0; i < 4; i++) disc(i & 1 ? cx + w / 2 - unit * 2 : cx - w / 2 + unit * 2, i & 2 ? y + h - unit * 2 : y + unit * 2, unit * 0.8, gold, gold);
    disc(cx - w * 0.3, y + unit * 0.6, unit * 0.6, gold, gold); disc(cx + w * 0.3, y + unit * 0.6, unit * 0.6, gold, gold);
    double s = h / 34;
    text("THE FOUR MARX BROTHERS", cx, y + h * 0.17, s * 0.62, 0.9, ink);
    text("IN", cx, y + h * 0.34, s * 0.62, 0.9, ink);
    text("DUCK SOUP", cx, y + h * 0.53, s * 1.6, 1.15, col(RED, 1));
    flush(ren);
}

/* ---- one scene ---- */
static int draw_order[MAXACT];
static void draw_stage(int s, double t)
{
    const Scene *sc = &scene[s];
    if (scene[s].kind == S_RADIO || scene[s].kind == S_MIRROR) { draw_mirror_room(s, t); draw_bubbles(s, t, 0, 1); return; }
    backdrop(sc->bg, ramp(t, 0, sc->len));
    double tall = rh * sc->tall;
    if (sc->kind == S_RIDE) {                       /* the window he stops at: lit, or dark */
        double x0 = rx0 + rw * WINDOW[0], y0 = ry0 + rh * WINDOW[1], x1 = rx0 + rw * WINDOW[2], y1 = ry0 + rh * WINDOW[3];
        if (window_lit(t)) {
            double glow = 0.5 + 0.5 * ramp(t, 7.8, 8.6) * (1 - ramp(t, 11.2, 12.4)) * (0.8 + 0.2 * sin(anim_t * 5));
            disc((x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) * 1.8, col((Rgb){255, 210, 120}, 0.35 * glow), col((Rgb){255, 210, 120}, 0));
        } else rect(x0, y0, x1 - x0, y1 - y0, col((Rgb){18, 22, 44}, 0.85));
        flush(ren);
    }
    int n = 0;
    for (int i = sc->first; i < sc->first + sc->n; i++) draw_order[n++] = i;
    for (int i = 1; i < n; i++)                     /* by layer, then as made */
        for (int j = i; j > 0 && act[draw_order[j - 1]].layer > act[draw_order[j]].layer; j--) {
            int k = draw_order[j]; draw_order[j] = draw_order[j - 1]; draw_order[j - 1] = k;
        }
    for (int i = 0; i < n; i++) draw_actor(&act[draw_order[i]], t, tall);
    flush(ren);
    if (sc->kind == S_RIDE || sc->kind == S_BED) rect(0, 0, sw, sh, (SDL_Color){10, 16, 50, 40});
    draw_bubbles(s, t, tall, 0);
    if (sc->kind == S_OPENER) {
        /* the title drops in with a bounce on its ropes, and goes up again before the curtain comes down */
        double down = ease(ramp(t, 3.0, 3.7)) * (1 - ease(ramp(t, sc->len - 4.6, sc->len - 3.8)));
        double since = t - 3.7, bounce = since > 0 && t < sc->len - 4.6 ? -sh * 0.04 * exp(-since * 3.5) * sin(since * 14) : 0;
        if (down > 0) placard(down, bounce);
        curtain(ramp(t, 0.6, 2.8) * (1 - ramp(t, sc->len - 2.6, sc->len - 0.4)));
    }
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    if (T >= scene_at[NSCENE]) {                    /* THE END */
        double q = T - scene_at[NSCENE];
        title_card("THE END", ease(ramp(q, 0, 0.6)) * (1 - ramp(q, SHOW_LEN - scene_at[NSCENE] - 0.8, SHOW_LEN - scene_at[NSCENE])));
        old_film();
        return;
    }
    double t;
    int s = scene_of(T, &t);
    if (t < 0) {                                    /* the title card */
        double a = ease(ramp(t, -CARD, -CARD + 0.4)) * (1 - ease(ramp(t, -0.45, 0)));
        title_card(scene[s].card, a);
        old_film();
        return;
    }
    draw_stage(s, t);
    if (scene[s].kind == S_OPENER) { old_film(); return; }        /* the curtain does the iris's work */
    /* the iris opens on the scene and closes on its last moment */
    double open = ease(ramp(t, 0, 0.9)), shut = ease(ramp(t, scene[s].len - 1.1, scene[s].len));
    double full = hypot(w, h);
    if (open < 1) iris(w / 2.0, h * 0.5, full * 0.6 * open);
    if (shut > 0) iris(rx0 + rw * scene[s].iris_x, ry0 + rh * scene[s].iris_y, full * 0.75 * (1 - shut));
    old_film();
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("Duck Soup", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    start_sound();
    new_show();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "duck-soup: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "duck-soup: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Duck Soup",
        "Duck Soup screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --mute          no sound\n"
        "  --speed X       animation speed, 1 = normal (without sound)\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n"
        "  --wav FILE.wav  write the sound track and exit\n", NULL);
}

static const char *opt_wav = NULL;
static int write_wav(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : 1;
    new_show();
    FILE *f = fopen(opt_wav, "wb");
    if (!f) return 1;
    unsigned int data = track_len * 2, riff = 36 + data, rate = RATE, brate = RATE * 2, fmtlen = 16;
    unsigned short pcm = 1, ch = 1, align = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtlen, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(track, 2, track_len, f);
    fclose(f);
    printf("%s: %.1f s\n", opt_wav, (double)track_len / RATE);
    for (int s = 0; s < NSCENE; s++) printf("  %5.1f  %s (%.1f s)\n", scene_at[s] + scene[s].card_len, scene[s].card ? scene[s].card : scene[s].kind == S_OPENER ? "(the opening titles)" : "(the sidecar)", scene[s].len);
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Duck Soup", "Duck Soup screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--mute")) opt_mute = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--wav") && i + 1 < argc) opt_wav = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();
    load_music();

    if (opt_wav) return write_wav();
    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "duck-soup: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "duck-soup: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}
