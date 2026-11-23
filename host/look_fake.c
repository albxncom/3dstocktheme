/* A look for the host tests only, never built into the application (host/test_look.c): it draws as Stock does,
 * through Stock's table and art, and notes the calls of its table, so a test sees when the drawing layer makes and
 * puts away a look's own textures and what it hands the entries. Its ground is a texture of its own over the whole
 * of both screens (ui_ground_image), without the pattern, so the screens every look shares can be checked under a
 * textured ground (G1 0.6): after it, no letters before an atlas image (kit.h, the drawing rule). It differs from
 * Stock where a port may (no pattern on the top screen, the wrench's place), and host/expect_fake.h says so, so
 * test_look proves that test_ui's checks read a look's own expectations (G1 0g).
 * Its loader is not Stock's (G1 0f): a hub of its own texture (a piece), a bead turned over it (ui_sprite_turned)
 * and three lit beads that go round the other way, so a test sees the drawing layer hand the loader to the look. */
#include "kit.h"
#include "look.h"

#include <math.h>
#include <string.h>

char fake_calls[64];            /* a letter a call, in order: l load, u unload, r refresh */
int fake_scheme = -2, fake_pattern = -2, fake_removes = -1;     /* what refresh and popup_buttons were handed last */
int fake_loaders;               /* the loader's calls, and what the last one was handed */
float fake_loader_cx, fake_loader_cy, fake_loader_alpha;
u64 fake_loader_ms;

#define GROUND_N 16             /* the ground's tile: two greys in squares of 4 px, repeated */
static C3D_Tex ground_tex;
static int ground_made;

static void note(char c)
{
    size_t n = strlen(fake_calls);
    if (n + 1 < sizeof fake_calls)
        fake_calls[n] = c, fake_calls[n + 1] = 0;
}

static void load(void)
{
    note('l');
    ground_made = C3D_TexInit(&ground_tex, GROUND_N, GROUND_N, GPU_RGBA8);
    if (!ground_made)
        return;
    uint32_t *texel = ground_tex.data;
    for (int i = 0; i < GROUND_N * GROUND_N; i++)
        texel[i] = (i / 4 + i / (4 * GROUND_N)) % 2 ? 0xC8C8C8FF : 0xB4B4B4FF;
    C3D_TexSetFilter(&ground_tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&ground_tex, GPU_REPEAT, GPU_REPEAT);
}

static void unload(void)
{
    note('u');
    if (ground_made)
        C3D_TexDelete(&ground_tex);
    ground_made = 0;
}

static void refresh(int scheme, int pattern)
{
    note('r');
    fake_scheme = scheme, fake_pattern = pattern;
}

static void ground(float y0, float y1, float alpha)
{
    if (!ground_made) {
        look_stock.draw.ground(y0, y1, alpha);
        return;
    }
    float w = TOP_W + 2 * BLEED;
    Tex3DS_SubTexture sub = {(u16)w, (u16)(y1 - y0), 0, 1 - y0 / GROUND_N, w / GROUND_N, 1 - y1 / GROUND_N};
    C2D_DrawParams params = {{-BLEED, y0, w, y1 - y0}, {0, 0}, 0, 0};
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    ui_ground_image(&ground_tex, &sub, &params, alpha < 1.0f ? &tint : NULL);
}

static void rule(float y)
{
    look_stock.draw.rule(y);
}

static void card(rect r, int glass)
{
    look_stock.draw.card(r, glass);
}

static rect face(int kind, rect r, int state, int symbol)
{
    return look_stock.draw.face(kind, r, state, symbol);
}

static void brackets(rect r, const int radius[4])
{
    ui_select_brackets(r, radius, -1);
}

static void bar(int parts, int held)
{
    look_stock.draw.bar(parts, held);
}

static void popup_panel(rect r)
{
    look_stock.draw.popup_panel(r);
}

static void popup_buttons(const char *left, const char *right, int held, int ring, int removes)
{
    fake_removes = removes;
    look_stock.draw.popup_buttons(left, right, held, ring, removes);
}

#define FAKE_HUB 12             /* the loader's hub, px */
static void loader(float cx, float cy, u64 ms, float alpha)
{
    fake_loaders++;
    fake_loader_cx = cx, fake_loader_cy = cy, fake_loader_ms = ms, fake_loader_alpha = alpha;
    C2D_ImageTint tint;
    C2D_AlphaImageTint(&tint, alpha);
    if (ground_made)
        ui_piece(&ground_tex, 0, 0, FAKE_HUB, FAKE_HUB, roundf(cx - FAKE_HUB / 2.0f), roundf(cy - FAKE_HUB / 2.0f),
                 FAKE_HUB, FAKE_HUB, &tint);
    float turn = -6.2831853f * (float)(ms % (2 * LOADER_MS)) / (2 * LOADER_MS);
    ui_sprite_turned(ART_LOADER_BEAD, cx, cy, -turn, &tint);     /* a groove at the hub, turning the other way */
    for (int i = 0; i < 3; i++) {
        float a = turn + 6.2831853f * i / 3;
        rect at = {roundf(cx + LOADER_BEAD_R * cosf(a) - LOADER_BEAD / 2.0f),
                   roundf(cy + LOADER_BEAD_R * sinf(a) - LOADER_BEAD / 2.0f), LOADER_BEAD, LOADER_BEAD};
        ui_sprite_draw(ART_LOADER_BEAD_ON, at, -1e9f, 1e9f, &tint);
    }
}

const look look_fake = {
    .name = "fake",
    .art = &ART_STOCK,
    .draw = {
        .load = load,
        .unload = unload,
        .refresh = refresh,
        .ground = ground,
        .rule = rule,
        .card = card,
        .face = face,
        .select = brackets,
        .bar = bar,
        .bar_label = ui_engraved,
        .popup_panel = popup_panel,
        .popup_buttons = popup_buttons,
        .loader = loader,
    },
    .m = {
        .dim_pct = DIM_ALPHA,
        .dlg_h = DLG_H + 4,             /* taller below, as Prisma's: the text stays where Stock's is (Q3) */
        .tab_face = ART_CORNER,
        .tab_face_down = ART_CORNER_DOWN,
        .tab_h = CORNER_H,
        .tab_icon_x = CORNER_PAD + 1,   /* not Stock's place (host/expect_fake.h) */
        .tab_icon_y = (CORNER_H - CORNER_ICON) / 2 - 2,
        .corner_h = CORNER_H,
        .corner_icon_x = CORNER_PAD,
        .corner_icon_y = (CORNER_H - CORNER_ICON) / 2,
        .help_size = HELP_FACE,
        .help_mark_y = (HELP_FACE - HELP_ICON) / 2,
        .help_mark_down = 1,
        .pane_rim_big = PANE_BIG_RIM,
        .pane_rim_small = PANE_SMALL_RIM,
    },
};
