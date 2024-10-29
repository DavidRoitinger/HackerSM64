#include <PR/ultratypes.h>

#include "sm64.h"
#include "actors/common1.h"
#include "gfx_dimensions.h"
#include "game_init.h"
#include "level_update.h"
#include "camera.h"
#include "print.h"
#include "ingame_menu.h"
#include "hud.h"
#include "segment2.h"
#include "area.h"
#include "save_file.h"
#include "print.h"
#include "engine/surface_load.h"
#include "engine/math_util.h"
#include "puppycam2.h"
#include "puppyprint.h"

#include "config.h"

/* @file hud.c
 * This file implements HUD rendering and power meter animations.
 * That includes stars, lives, coins, camera status, power meter, timer
 * cannon reticle, and the unused keys.
 **/

#ifdef BREATH_METER
#define HUD_BREATH_METER_X         40
#define HUD_BREATH_METER_Y         32
#define HUD_BREATH_METER_HIDDEN_Y -20
#endif

// ------------- FPS COUNTER ---------------
// To use it, call print_fps(x,y); every frame.
#define FRAMETIME_COUNT 30

OSTime frameTimes[FRAMETIME_COUNT];
u8 curFrameTimeIndex = 0;

#include "PR/os_convert.h"

#ifdef USE_PROFILER
float profiler_get_fps();
#else
// Call once per frame
f32 calculate_and_update_fps() {
    OSTime newTime = osGetTime();
    OSTime oldTime = frameTimes[curFrameTimeIndex];
    frameTimes[curFrameTimeIndex] = newTime;

    curFrameTimeIndex++;
    if (curFrameTimeIndex >= FRAMETIME_COUNT) {
        curFrameTimeIndex = 0;
    }
    return ((f32)FRAMETIME_COUNT * 1000000.0f) / (s32)OS_CYCLES_TO_USEC(newTime - oldTime);
}
#endif

void print_fps(s32 x, s32 y) {
#ifdef USE_PROFILER
    f32 fps = profiler_get_fps();
#else
    f32 fps = calculate_and_update_fps();
#endif
    char text[14];

    sprintf(text, "FPS %2.2f", fps);
#ifdef PUPPYPRINT
    print_small_text(x, y, text, PRINT_TEXT_ALIGN_LEFT, PRINT_ALL, FONT_OUTLINE);
#else
    print_text(x, y, text);
#endif
}

// ------------ END OF FPS COUNER -----------------

struct PowerMeterHUD {
    s8 animation;
    s16 x;
    s16 y;
};

struct CameraHUD {
    s16 status;
};

// Stores health segmented value defined by numHealthWedges
// When the HUD is rendered this value is 8, full health.
static s16 sPowerMeterStoredHealth;

static struct PowerMeterHUD sPowerMeterHUD = {
    POWER_METER_HIDDEN,
    HUD_POWER_METER_X,
    HUD_POWER_METER_HIDDEN_Y,
};

// Power Meter timer that keeps counting when it's visible.
// Gets reset when the health is filled and stops counting
// when the power meter is hidden.
s32 sPowerMeterVisibleTimer = 0;

#ifdef BREATH_METER
static s16 sBreathMeterStoredValue;
static struct PowerMeterHUD sBreathMeterHUD = {
    BREATH_METER_HIDDEN,
    HUD_BREATH_METER_X,
    HUD_BREATH_METER_HIDDEN_Y,
};
s32 sBreathMeterVisibleTimer = 0;
#endif

static struct CameraHUD sCameraHUD = { CAM_STATUS_NONE };

/**
 * Renders a rgba16 16x16 glyph texture from a table list.
 */
void render_hud_tex_lut(s32 x, s32 y, Texture *texture) {
    Gfx *tempGfxHead = gDisplayListHead;

    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gSPDisplayList(tempGfxHead++, &dl_hud_img_load_tex_block);
    gSPTextureRectangle(tempGfxHead++, x << 2, y << 2, (x + 15) << 2, (y + 15) << 2,
                        G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders a rgba16 8x8 glyph texture from a table list.
 */
void render_hud_small_tex_lut(s32 x, s32 y, Texture *texture) {
    Gfx *tempGfxHead = gDisplayListHead;

    gDPSetTile(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
                G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD, G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD);
    gDPTileSync(tempGfxHead++);
    gDPSetTile(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2, 0, G_TX_RENDERTILE, 0,
                G_TX_CLAMP, 3, G_TX_NOLOD, G_TX_CLAMP, 3, G_TX_NOLOD);
    gDPSetTileSize(tempGfxHead++, G_TX_RENDERTILE, 0, 0, (8 - 1) << G_TEXTURE_IMAGE_FRAC, (8 - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, texture);
    gDPLoadSync(tempGfxHead++);
    gDPLoadBlock(tempGfxHead++, G_TX_LOADTILE, 0, 0, 8 * 8 - 1, CALC_DXT(8, G_IM_SIZ_16b_BYTES));
    gSPTextureRectangle(tempGfxHead++, x << 2, y << 2, (x + 7) << 2, (y + 7) << 2, G_TX_RENDERTILE,
                        0, 0, 4 << 10, 1 << 10);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders power meter health segment texture using a table list.
 */
void render_power_meter_health_segment(s16 numHealthWedges) {
    Texture *(*healthLUT)[] = segmented_to_virtual(&power_meter_health_segments_lut);
    Gfx *tempGfxHead = gDisplayListHead;

    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1,
                       (*healthLUT)[numHealthWedges - 1]);
    gDPLoadSync(tempGfxHead++);
    gDPLoadBlock(tempGfxHead++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES));
    gSP1Triangle(tempGfxHead++, 0, 1, 2, 0);
    gSP1Triangle(tempGfxHead++, 0, 2, 3, 0);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders power meter display lists.
 * That includes the "POWER" base and the colored health segment textures.
 */
void render_dl_power_meter(s16 numHealthWedges) {
    Mtx *mtx = alloc_display_list(sizeof(Mtx));

    if (mtx == NULL) {
        return;
    }

    guTranslate(mtx, (f32) sPowerMeterHUD.x, (f32) sPowerMeterHUD.y, 0);

    gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx++),
              G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList(gDisplayListHead++, &dl_power_meter_base);

    if (numHealthWedges != 0) {
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_begin);
        render_power_meter_health_segment(numHealthWedges);
        gSPDisplayList(gDisplayListHead++, &dl_power_meter_health_segments_end);
    }

    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

/**
 * Power meter animation called when there's less than 8 health segments
 * Checks its timer to later change into deemphasizing mode.
 */
void animate_power_meter_emphasized(void) {
    s16 hudDisplayFlags = gHudDisplay.flags;

    if (!(hudDisplayFlags & HUD_DISPLAY_FLAG_EMPHASIZE_POWER)) {
        if (sPowerMeterVisibleTimer == 45.0f) {
            sPowerMeterHUD.animation = POWER_METER_DEEMPHASIZING;
        }
    } else {
        sPowerMeterVisibleTimer = 0;
    }
}

/**
 * Power meter animation called after emphasized mode.
 * Moves power meter y enemyPos speed until it's at 200 to be visible.
 */
static void animate_power_meter_deemphasizing(void) {
    s16 speed = 5;

    if (sPowerMeterHUD.y > HUD_POWER_METER_Y - 20) speed = 3;
    if (sPowerMeterHUD.y > HUD_POWER_METER_Y - 10) speed = 2;
    if (sPowerMeterHUD.y > HUD_POWER_METER_Y -  5) speed = 1;

    sPowerMeterHUD.y += speed;

    if (sPowerMeterHUD.y > HUD_POWER_METER_Y) {
        sPowerMeterHUD.y = HUD_POWER_METER_Y;
        sPowerMeterHUD.animation = POWER_METER_VISIBLE;
    }
}

/**
 * Power meter animation called when there's 8 health segments.
 * Moves power meter y enemyPos quickly until it's at 301 to be hidden.
 */
static void animate_power_meter_hiding(void) {
    sPowerMeterHUD.y += 20;
    if (sPowerMeterHUD.y > HUD_POWER_METER_HIDDEN_Y) {
        sPowerMeterHUD.animation = POWER_METER_HIDDEN;
        sPowerMeterVisibleTimer = 0;
    }
}

/**
 * Handles power meter actions depending of the health segments values.
 */
void handle_power_meter_actions(s16 numHealthWedges) {
    // Show power meter if health is not full, less than 8
    if (numHealthWedges < 8 && sPowerMeterStoredHealth == 8
        && sPowerMeterHUD.animation == POWER_METER_HIDDEN) {
        sPowerMeterHUD.animation = POWER_METER_EMPHASIZED;
        sPowerMeterHUD.y = HUD_POWER_METER_EMPHASIZED_Y;
    }

    // Show power meter if health is full, has 8
    if (numHealthWedges == 8 && sPowerMeterStoredHealth == 7) {
        sPowerMeterVisibleTimer = 0;
    }

    // After health is full, hide power meter
    if (numHealthWedges == 8 && sPowerMeterVisibleTimer > 45.0f) {
        sPowerMeterHUD.animation = POWER_METER_HIDING;
    }

    // Update to match health value
    sPowerMeterStoredHealth = numHealthWedges;

#ifndef BREATH_METER
    // If Mario is swimming, keep power meter visible
    if (gPlayerCameraState->action & ACT_FLAG_SWIMMING) {
        if (sPowerMeterHUD.animation == POWER_METER_HIDDEN
            || sPowerMeterHUD.animation == POWER_METER_EMPHASIZED) {
            sPowerMeterHUD.animation = POWER_METER_DEEMPHASIZING;
            sPowerMeterHUD.y = HUD_POWER_METER_EMPHASIZED_Y;
        }
        sPowerMeterVisibleTimer = 0;
    }
#endif
}

/**
 * Renders the power meter that shows when Mario is in underwater
 * or has taken damage and has less than 8 health segments.
 * And calls a power meter animation function depending of the value defined.
 */
void render_hud_power_meter(void) {
    s16 shownHealthWedges = gHudDisplay.wedges;
    if (sPowerMeterHUD.animation != POWER_METER_HIDING) handle_power_meter_actions(shownHealthWedges);
    if (sPowerMeterHUD.animation == POWER_METER_HIDDEN) return;
    switch (sPowerMeterHUD.animation) {
        case POWER_METER_EMPHASIZED:    animate_power_meter_emphasized();    break;
        case POWER_METER_DEEMPHASIZING: animate_power_meter_deemphasizing(); break;
        case POWER_METER_HIDING:        animate_power_meter_hiding();        break;
        default:                                                             break;
    }
    render_dl_power_meter(shownHealthWedges);
    sPowerMeterVisibleTimer++;
}

#ifdef BREATH_METER
/**
 * Renders breath meter health segment texture using a table list.
 */
void render_breath_meter_segment(s16 numBreathWedges) {
    Texture *(*breathLUT)[];
    breathLUT = segmented_to_virtual(&breath_meter_segments_lut);
    Gfx *tempGfxHead = gDisplayListHead;

    gDPPipeSync(tempGfxHead++);
    gDPSetTextureImage(tempGfxHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, (*breathLUT)[numBreathWedges - 1]);
    gDPLoadSync(tempGfxHead++);
    gDPLoadBlock(tempGfxHead++, G_TX_LOADTILE, 0, 0, 32 * 32 - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES));
    gSP1Triangle(tempGfxHead++, 0, 1, 2, 0);
    gSP1Triangle(tempGfxHead++, 0, 2, 3, 0);

    gDisplayListHead = tempGfxHead;
}

/**
 * Renders breath meter display lists.
 * That includes the base and the colored segment textures.
 */
void render_dl_breath_meter(s16 numBreathWedges) {
    Mtx *mtx = alloc_display_list(sizeof(Mtx));

    if (mtx == NULL) {
        return;
    }

    guTranslate(mtx, (f32) sBreathMeterHUD.x, (f32) sBreathMeterHUD.y, 0);
    gSPMatrix(      gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx++),
                    G_MTX_MODELVIEW | G_MTX_MUL | G_MTX_PUSH);
    gSPDisplayList( gDisplayListHead++, &dl_breath_meter_base);
    if (numBreathWedges != 0) {
        gSPDisplayList(gDisplayListHead++, &dl_breath_meter_health_segments_begin);
        render_breath_meter_segment(numBreathWedges);
        gSPDisplayList(gDisplayListHead++, &dl_breath_meter_health_segments_end);
    }
    gSPPopMatrix(gDisplayListHead++, G_MTX_MODELVIEW);
}

/**
 * Breath meter animation called after emphasized mode.
 * Moves breath meter y enemyPos speed until it's visible.
 */
static void animate_breath_meter_sliding_in(void) {
    approach_s16_symmetric_bool(&sBreathMeterHUD.y, HUD_BREATH_METER_Y, 5);
    if (sBreathMeterHUD.y         == HUD_BREATH_METER_Y) {
        sBreathMeterHUD.animation = BREATH_METER_VISIBLE;
    }
}

/**
 * Breath meter animation called when there's 8 health segments.
 * Moves breath meter y enemyPos quickly until it's hidden.
 */
static void animate_breath_meter_sliding_out(void) {
    approach_s16_symmetric_bool(&sBreathMeterHUD.y, HUD_BREATH_METER_HIDDEN_Y, 20);
    if (sBreathMeterHUD.y         == HUD_BREATH_METER_HIDDEN_Y) {
        sBreathMeterHUD.animation = BREATH_METER_HIDDEN;
    }
}

/**
 * Handles breath meter actions depending of the health segments values.
 */
void handle_breath_meter_actions(s16 numBreathWedges) {
    // Show breath meter if health is not full, less than 8
    if ((numBreathWedges < 8) && (sBreathMeterStoredValue == 8) && sBreathMeterHUD.animation == BREATH_METER_HIDDEN) {
        sBreathMeterHUD.animation = BREATH_METER_SHOWING;
        // sBreathMeterHUD.y         = HUD_BREATH_METER_Y;
    }
    // Show breath meter if breath is full, has 8
    if ((numBreathWedges == 8) && (sBreathMeterStoredValue  == 7)) sBreathMeterVisibleTimer  = 0;
    // After breath is full, hide breath meter
    if ((numBreathWedges == 8) && (sBreathMeterVisibleTimer > 45)) sBreathMeterHUD.animation = BREATH_METER_HIDING;
    // Update to match breath value
    sBreathMeterStoredValue = numBreathWedges;
    // If Mario is swimming, keep breath meter visible
    if (gPlayerCameraState->action & ACT_FLAG_SWIMMING) {
        if (sBreathMeterHUD.animation == BREATH_METER_HIDDEN) {
            sBreathMeterHUD.animation = BREATH_METER_SHOWING;
        }
        sBreathMeterVisibleTimer = 0;
    }
}

void render_hud_breath_meter(void) {
    s16 shownBreathAmount = gHudDisplay.breath;
    if (sBreathMeterHUD.animation != BREATH_METER_HIDING) handle_breath_meter_actions(shownBreathAmount);
    if (sBreathMeterHUD.animation == BREATH_METER_HIDDEN) return;
    switch (sBreathMeterHUD.animation) {
        case BREATH_METER_SHOWING:       animate_breath_meter_sliding_in();  break;
        case BREATH_METER_HIDING:        animate_breath_meter_sliding_out(); break;
        default:                                                             break;
    }
    render_dl_breath_meter(shownBreathAmount);
    sBreathMeterVisibleTimer++;
}
#endif


/**
 * Renders the amount of lives Mario has.
 */
void render_hud_mario_lives(void) {
    print_text(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(22), HUD_TOP_Y, ","); // 'Mario Head' glyph
    print_text(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(38), HUD_TOP_Y, "*"); // 'X' glyph
    print_text_fmt_int(GFX_DIMENSIONS_RECT_FROM_LEFT_EDGE(54), HUD_TOP_Y, "%d", gHudDisplay.lives);
}

#ifdef VANILLA_STYLE_CUSTOM_DEBUG
void render_debug_mode(void) {
    print_text(180, 40, "DEBUG MODE");
    print_text_fmt_int(5, 20, "Z %d", gMarioState->enemyPos[2]);
    print_text_fmt_int(5, 40, "Y %d", gMarioState->enemyPos[1]);
    print_text_fmt_int(5, 60, "X %d", gMarioState->enemyPos[0]);
    print_text_fmt_int(10, 100, "SPD %d", (s32) gMarioState->forwardVel);
    print_text_fmt_int(10, 120, "ANG 0*%04x", (u16) gMarioState->faceAngle[1]);
    print_fps(10,80);
}
#endif

/**
 * Renders the amount of coins collected.
 */
void render_hud_coins(void) {
    print_text(HUD_COINS_X, HUD_TOP_Y, "$"); // 'Coin' glyph
    print_text((HUD_COINS_X + 16), HUD_TOP_Y, "*"); // 'X' glyph
    print_text_fmt_int((HUD_COINS_X + 30), HUD_TOP_Y, "%d", gHudDisplay.coins);
}

/**
 * Renders the amount of stars collected.
 * Disables "X" glyph when Mario has 100 stars or more.
 */
void render_hud_stars(void) {
    if (gHudFlash == HUD_FLASH_STARS && gGlobalTimer & 0x8) return;
    s8 showX = (gHudDisplay.stars < 100);
    print_text(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X), HUD_TOP_Y, "^"); // 'Star' glyph
    if (showX) print_text((GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X) + 16), HUD_TOP_Y, "*"); // 'X' glyph
    print_text_fmt_int((showX * 14) + GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_STARS_X - 16),
                       HUD_TOP_Y, "%d", gHudDisplay.stars);
}

/**
 * Unused function that renders the amount of keys collected.
 * Leftover function from the beta version of the game.
 */
void render_hud_keys(void) {
    s16 i;

    for (i = 0; i < gHudDisplay.keys; i++) {
        print_text((i * 16) + 220, 142, "|"); // unused glyph - beta key
    }
}

/**
 * Renders the timer when Mario start sliding in PSS.
 */
void render_hud_timer(void) {
    Texture *(*hudLUT)[58] = segmented_to_virtual(&main_hud_lut);
    u16 timerValFrames = gHudDisplay.timer;
    u16 timerMins = timerValFrames / (30 * 60);
    u16 timerSecs = (timerValFrames - (timerMins * 1800)) / 30;
    u16 timerFracSecs = ((timerValFrames - (timerMins * 1800) - (timerSecs * 30)) & 0xFFFF) / 3;

#if MULTILANG
    switch (eu_get_language()) {
        case LANGUAGE_ENGLISH: print_text(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(150), 185,  "TIME"); break;
        case LANGUAGE_FRENCH:  print_text(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(155), 185, "TEMPS"); break;
        case LANGUAGE_GERMAN:  print_text(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(150), 185,  "ZEIT"); break;
    }
#else
    print_text(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(150), 185, "TIME");
#endif

    print_text_fmt_int(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(91), 185, "%0d", timerMins);
    print_text_fmt_int(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(71), 185, "%02d", timerSecs);
    print_text_fmt_int(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(37), 185, "%d", timerFracSecs);

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    render_hud_tex_lut(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(81), 32, (*hudLUT)[GLYPH_APOSTROPHE]);
    render_hud_tex_lut(GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(46), 32, (*hudLUT)[GLYPH_DOUBLE_QUOTE]);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

/**
 * Sets HUD status camera value depending of the actions
 * defined in update_camera_status.
 */
void set_hud_camera_status(s16 status) {
    sCameraHUD.status = status;
}

/**
 * Renders camera HUD glyphs using a table list, depending of
 * the camera status called, a defined glyph is rendered.
 */
void render_hud_camera_status(void) {
    Texture *(*cameraLUT)[6] = segmented_to_virtual(&main_hud_camera_lut);
    s32 x = GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(HUD_CAMERA_X);
    s32 y = 205;

    if (sCameraHUD.status == CAM_STATUS_NONE) {
        return;
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    render_hud_tex_lut(x, y, (*cameraLUT)[GLYPH_CAM_CAMERA]);

    switch (sCameraHUD.status & CAM_STATUS_MODE_GROUP) {
        case CAM_STATUS_MARIO:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_MARIO_HEAD]);
            break;
        case CAM_STATUS_LAKITU:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_LAKITU_HEAD]);
            break;
        case CAM_STATUS_FIXED:
            render_hud_tex_lut(x + 16, y, (*cameraLUT)[GLYPH_CAM_FIXED]);
            break;
    }

    switch (sCameraHUD.status & CAM_STATUS_C_MODE_GROUP) {
        case CAM_STATUS_C_DOWN:
            render_hud_small_tex_lut(x + 4, y + 16, (*cameraLUT)[GLYPH_CAM_ARROW_DOWN]);
            break;
        case CAM_STATUS_C_UP:
            render_hud_small_tex_lut(x + 4, y - 8, (*cameraLUT)[GLYPH_CAM_ARROW_UP]);
            break;
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}

/**
 * Render HUD strings using hudDisplayFlags with it's render functions,
 * excluding the cannon reticle which detects a camera preset for it.
 */
void render_hud(void) {
    s16 hudDisplayFlags = gHudDisplay.flags;

    fooTest();

    if (hudDisplayFlags == HUD_DISPLAY_NONE) {
        sPowerMeterHUD.animation = POWER_METER_HIDDEN;
        sPowerMeterStoredHealth = 8;
        sPowerMeterVisibleTimer = 0;
#ifdef BREATH_METER
        sBreathMeterHUD.animation = BREATH_METER_HIDDEN;
        sBreathMeterStoredValue = 8;
        sBreathMeterVisibleTimer = 0;
#endif
    } else {
#ifdef VERSION_EU
        // basically create_dl_ortho_matrix but guOrtho screen width is different
        Mtx *mtx = alloc_display_list(sizeof(*mtx));

        if (mtx == NULL) {
            return;
        }

        create_dl_identity_matrix();
        guOrtho(mtx, -16.0f, SCREEN_WIDTH + 16, 0, SCREEN_HEIGHT, -10.0f, 10.0f, 1.0f);
        gSPPerspNormalize(gDisplayListHead++, 0xFFFF);
        gSPMatrix(gDisplayListHead++, VIRTUAL_TO_PHYSICAL(mtx),
                  G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
#else
        create_dl_ortho_matrix();
#endif
        // ----- EDIT

//         if (gCurrentArea != NULL && gCurrentArea->camera->mode == CAMERA_MODE_INSIDE_CANNON) {
//             render_hud_cannon_reticle();
//         }

// #ifdef ENABLE_LIVES
//         if (hudDisplayFlags & HUD_DISPLAY_FLAG_LIVES) {
//             render_hud_mario_lives();
//         }
// #endif

//         if (hudDisplayFlags & HUD_DISPLAY_FLAG_COIN_COUNT) {
//             render_hud_coins();
//         }

        

//         if (hudDisplayFlags & HUD_DISPLAY_FLAG_STAR_COUNT) {
//             render_hud_stars();
//         }

//         if (hudDisplayFlags & HUD_DISPLAY_FLAG_KEYS) {
//             render_hud_keys();
//         }

// #ifdef BREATH_METER
//         if (hudDisplayFlags & HUD_DISPLAY_FLAG_BREATH_METER) render_hud_breath_meter();
// #endif

//         if (hudDisplayFlags & HUD_DISPLAY_FLAG_CAMERA_AND_POWER) {
//             render_hud_power_meter();
// #ifdef PUPPYCAM
//             if (!gPuppyCam.enabled) {
// #endif
//             render_hud_camera_status();
// #ifdef PUPPYCAM
//             }
// #endif
//         }

//         if (hudDisplayFlags & HUD_DISPLAY_FLAG_TIMER) {
//             render_hud_timer();
//         }

// #ifdef VANILLA_STYLE_CUSTOM_DEBUG
//         if (gCustomDebugMode) {
//             render_debug_mode();
//         }
// #endif
    }
}



// ----- EDIT

s32 grid[10][10] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    {1, 0, 1, 0, 1, 0, 1, 1, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 0, 1, 0, 1, 0, 1},
    {1, 0, 0, 1, 0, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 1, 0, 1, 1, 1},
    {1, 0, 1, 1, 1, 1, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    };

const s32 MAP_SIZE = 10;

struct Entity enemyList[100];

const s32 HEIGHT = 240; // 224?
const s32 WIDTH = 320; // 304?
const s32 TILE_SIZE = 16;
const f32 TURN_SPEED = 3.0f;
const f32 PI = 3.141592653589793f;
const s32 FOV = 60;
const f32 SLICE_SIZE = 1;
const s32 NUM_RAYS = FOV / SLICE_SIZE;
const s32 COLUMN_WIDTH = WIDTH / (f32) NUM_RAYS;


s32 fuck = 1;

Vec3f enemyPos = {0.0f, 0.0f, 0.0f};

f32 angleInDegrees = 0.0f;

struct Ray ray = {.hitPosition = {0.0f, 0.0f, 0.0f}, .distance=0.0f, .hit=FALSE};

struct Entity player = { .spriteIndex = 5, .pos={0.0f, 0.0f, 0.0f}, .angleInDegrees = 0.0f};

char buffer[100];

void fooTest(){

    // gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    // add_texture(1);
    // render_tile_cords(100, 100, 111, 110);
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    handle_stick_movement(&player, 2.0f);
    // draw_render_demo();

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    add_texture(2);
    render_tile_sized(0, 0, WIDTH, HEIGHT);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    for (int i = 0; i < FOV / SLICE_SIZE; i++){
	    castRay(player.pos, player.angleInDegrees + FOV/-2 + (i * SLICE_SIZE), grid, &ray);
        
        if(ray.hit){
            f32 wallHeight = (TILE_SIZE * HEIGHT) / ray.distance;
            if(wallHeight > HEIGHT){
                wallHeight = HEIGHT;
            }

            f32 wallOffset = HEIGHT / 2.0f - wallHeight / 2.0f;

            gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
            add_texture(1);
            render_tile_sized(COLUMN_WIDTH * i, wallOffset, COLUMN_WIDTH, wallHeight);
            // render_tile_cords((WIDTH / (FOV / SLICE_SIZE)) * i, (HEIGHT/2) + 25 * (ray.distance/10), (WIDTH / (FOV / SLICE_SIZE)) * (i + 1), (HEIGHT/2) - 25 * (ray.distance/10));
            gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
        }
    }


}

void draw_render_demo(){
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    Vec3f tilePos;
    for (int i = 0; i < (int)(sizeof(grid) / sizeof(grid[0])); i++) {
        for (int j = 0; j < (int)(sizeof(grid[0]) / sizeof(grid[0][0])); j++) {
            add_texture(grid[i][j] == 1 ? 2 : 1);
            vec3f_set(tilePos, 8 + (j * TILE_SIZE), 0, 8 + i * TILE_SIZE);
            render_tile(tilePos[0], tilePos[2]);
        }
    }
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    for (int i = FOV/-2; i < FOV; i+= SLICE_SIZE){
	    castRay(player.pos, player.angleInDegrees + i, grid, &ray);

        gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
        add_texture(6);
        render_tile(ray.hitPosition[0], ray.hitPosition[1]);
        gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
    }

    //Player
    draw_entity(&player);
}


void draw_entity(struct Entity *entity){
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    add_texture(entity->spriteIndex);
    render_tile(entity->pos[0], entity->pos[1]);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}


void draw_background(){
    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);

    s32 currentSprite = 2;
    add_texture(currentSprite);

    Vec3f tilePos;
    

    for (int i = 0; i <= HEIGHT; i += TILE_SIZE) {
        for (int j = 0; j <= WIDTH; j += TILE_SIZE) {
            if(i == 0 || i == HEIGHT || j == 0 || j >= WIDTH){
                vec3f_set(tilePos, j, 0, i);
                render_tile(tilePos[0], tilePos[2]);
            }
        }
    }

    for (int i = 0; i <= 224; i += 16) {
            if(currentSprite == 1){
                currentSprite = 2;
                add_texture(2);
            }else{
                currentSprite = 1;
                add_texture(1);
            }
        for (int j = 0; j < 416; j += 16) {
            if(currentSprite == 1){
                currentSprite = 2;
                add_texture(2);
            }else{
                currentSprite = 1;
                add_texture(1);
            }

            vec3f_set(tilePos, j, 0, i);
            //move_tile(&tilePos, enemyPos, 10);
            render_tile(tilePos[0], tilePos[2]);
        }
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}


void add_texture(s8 glyphIndex){
    const Texture *const *glyphs = segmented_to_virtual(edit_custom_textures);

    gDPPipeSync(gDisplayListHead++);
    gDPSetTextureImage(gDisplayListHead++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, glyphs[glyphIndex]);
    gSPDisplayList(gDisplayListHead++, dl_hud_img_load_tex_block);
}


void render_tile(s32 x, s32 y) {
    s32 rectBaseX = x;
    s32 rectBaseY = y;
    s32 rectX;
    s32 rectY;

    rectX = rectBaseX;
    rectY = rectBaseY;
    gSPScisTextureRectangle(gDisplayListHead++, rectX << 2, rectY << 2, (rectX + 15) << 2,
                        (rectY + 15) << 2, G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);
    // gSPTextureRectangle(gDisplayListHead++, 0 << 2, 0 << 2, (WIDTH + 12) << 2,
    //                     (HEIGHT + 13) << 2, G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);
}

void render_tile_sized(s32 x, s32 y, s32 xSize, s32 ySize) {
    s32 rectBaseX = x;
    s32 rectBaseY = y;
    s32 rectX;
    s32 rectY;
    xSize--;
    ySize--;

    rectX = rectBaseX;
    rectY = rectBaseY;
    gSPScisTextureRectangle(gDisplayListHead++, rectX << 2, rectY << 2, (rectX + xSize) << 2,
                        (rectY + ySize) << 2, G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);
    // gSPScisTextureRectangle(gDisplayListHead++, rectX << 2, rectY << 2, (rectX + xSize) << 2,
    //                     (rectY + ySize) << 2, G_TX_RENDERTILE, 0, 0, fuck << 3, fuck << 1);
}

void render_tile_cords(s32 x, s32 y, s32 x2, s32 y2) {
    x2--;
    y2--;

    gSPScisTextureRectangle(gDisplayListHead++, x << 2, y << 2, (x2) << 2,
                        (y2) << 2, G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);
}


void move_tile(Vec3f *pos, Vec3f targetPos, f32 velocity) {
    const f32 deltaTime = 0.03;

    Vec3f difference;
    vec3f_set(difference, 0, 0, 0);

    vec3f_diff(difference, targetPos, *pos);
    vec3f_normalize(difference);

    Vec3f movement;
    vec3f_copy(movement, difference)
    vec3_scale(movement, velocity);

    Vec3f timedMovement;
    vec3f_copy(timedMovement, movement)
    vec3_scale(movement, deltaTime);

    vec3f_sum(*pos, *pos, timedMovement)
}


void handle_stick_movement(struct Entity *entity, f32 velocity){
    Vec3f stickVec = {0.0f, 0.0f, 0.0f};
    Vec3f rotVec = {0.0f, 0.0f, 0.0f};
    
    struct Controller *controller = gMarioState->controller;


    if(controller->buttonDown & L_CBUTTONS){
        entity->angleInDegrees -= TURN_SPEED;
    }

    if(controller->buttonDown & R_CBUTTONS){
        entity->angleInDegrees += TURN_SPEED;
    }

    f32 mag = ((controller->stickMag / 64.0f) * (controller->stickMag / 64.0f)) * 64.0f;

    //inverts Y-axis, because why not?
    vec3f_set(stickVec, controller->stickX, -controller->stickY, 0);
    vec3f_normalize(stickVec);
    vec3_scale(stickVec, mag / 64.0f)

    vec3_scale(stickVec, velocity);

    s16 angle = degrees_to_angle(270 - entity->angleInDegrees); //360° - x - 90°

    f32 rotationMatrix[3][3] ={
        {coss(angle), -sins(angle), 0},
        {sins(angle), coss(angle), 0},
        {0, 0, 1},
    };

    // sprintf(buffer, "%.1f %.1f", stickVec[0], stickVec[1]);
    // print_text(0, 20, buffer);

    linear_mtxf_mul_vec3(rotationMatrix, rotVec, stickVec);
    
    // sprintf(buffer, "%.1f %.1f", rotVec[0], rotVec[1]);
    // print_text(0, 40, buffer);

    vec3f_sum(entity->pos, entity->pos, rotVec);
    
}

f32 floor(f32 f){
    return (f32)(s32)f;
}


const s32 MAX_RAYCAST_DEPTH = 16;

void castRay(Vec3f start, float angleInDegrees, const s32 grid[MAP_SIZE][MAP_SIZE], struct Ray *ray){
    s16 angle = degrees_to_angle(angleInDegrees);
    float vtan = -tans(angle), htan = -1.0f / tans(angle);
    float cellSize = 16;

    Bool8 hit = FALSE;
    s32 vdof = 0, hdof = 0;
    float vdist = (float) S32_MAX;
    float hdist = (float) S32_MAX;

    Vec3f vRayPos, hRayPos, offset;

    if (coss(angle) > 0.001f) {
        vRayPos[0] = floor(start[0] / cellSize) * cellSize + cellSize;
        vRayPos[1] = (start[0] - vRayPos[0]) * vtan + start[1];
        offset[0] = cellSize;
        offset[1] = -offset[0] * vtan;
    } else if (coss(angle) < -0.001f) {
        vRayPos[0] = floor(start[0] / cellSize) * cellSize - 0.01f;
        vRayPos[1] = (start[0] - vRayPos[0]) * vtan + start[1];
        offset[0] = -cellSize;
        offset[1] = -offset[0] * vtan;
    } else {
        vdof = MAX_RAYCAST_DEPTH;
    }


    for (; vdof < MAX_RAYCAST_DEPTH; vdof++) {
        int mapX = (int)(vRayPos[0] / cellSize);
        int mapY = (int)(vRayPos[1] / cellSize);
        // if (mapY < (int)(sizeof(grid) / sizeof(grid[0])) && mapX <  (int)(sizeof(grid[mapY]) / sizeof(grid[mapY][0])) && grid[mapY][mapX] != 0) {
        if (mapY < MAP_SIZE && mapX < MAP_SIZE && grid[mapY][mapX] != 0) {
            hit = TRUE;
            vdist = sqrtf((vRayPos[0] - start[0]) * (vRayPos[0] - start[0]) +
                                (vRayPos[1] - start[1]) * (vRayPos[1] - start[1]));
            break;
        }
        vRayPos[0] += offset[0];
        vRayPos[1] += offset[1];
    }
    

    // vRayPos[0] += offset[0];
    // vRayPos[1] += offset[1];
    // hit = TRUE;
    // vdist = sqrtf((vRayPos[0] - start[0]) * (vRayPos[0] - start[0]) +
    //                 (vRayPos[1] - start[1]) * (vRayPos[1] - start[1]));

    
    if (sins(angle) > 0.001f) {
    hRayPos[1] = floor(start[1] / cellSize) * cellSize + cellSize;
    hRayPos[0] = (start[1] - hRayPos[1]) * htan + start[0];
    offset[1] = cellSize;
    offset[0] = -offset[1] * htan;
  } else if (sins(angle) < -0.001f) {
    hRayPos[1] = floor(start[1] / cellSize) * cellSize - 0.01f;
    hRayPos[0] = (start[1] - hRayPos[1]) * htan + start[0];
    offset[1] = -cellSize;
    offset[0] = -offset[1] * htan;
  } else {
    hdof = MAX_RAYCAST_DEPTH;
  }

  for (; hdof < MAX_RAYCAST_DEPTH; hdof++) {
    int mapX = (int)(hRayPos[0] / cellSize);
    int mapY = (int)(hRayPos[1] / cellSize);
    // if (mapY < grid.size() && mapX < grid[mapY].size() && grid[mapY][mapX]) {
    if (mapY < MAP_SIZE && mapX < MAP_SIZE && grid[mapY][mapX] != 0) {
      hit = TRUE;
      hdist = sqrtf((hRayPos[0] - start[0]) * (hRayPos[0] - start[0]) +
                        (hRayPos[1] - start[1]) * (hRayPos[1] - start[1]));
      break;
    }
    hRayPos[0] += offset[0];
    hRayPos[1] += offset[1];
  }


    // hRayPos[0] += offset[0];
    // hRayPos[1] += offset[1];
    // hit = TRUE;
    // hdist = sqrtf((hRayPos[0] - start[0]) * (hRayPos[0] - start[0]) +
    //                 (hRayPos[1] - start[1]) * (hRayPos[1] - start[1]));

    // sprintf(buffer, "off %.1f %.1f", hRayPos[0], hRayPos[1]);
    // print_text(100, 60, buffer);

    if (hdist < vdist){
        ray->hitPosition[0] = hRayPos[0];
        ray->hitPosition[1] = hRayPos[1];
    }else{
        ray->hitPosition[0] = vRayPos[0];
        ray->hitPosition[1] = vRayPos[1];
    }


    ray->distance = MIN(hdist, vdist);
    ray->hit = hit;
}

// OLD MEANINGLESS COMMENTS



    //fuck????
    // fuck++;
    // //print_text(200, 100, fuck+"");
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    // add_texture(4);
    // render_tile_sized(0,224, WIDTH, HEIGHT);
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    //Stripes Demo
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    // add_texture(4);
    // for (int i = 0; i <= WIDTH; i ++) {
    //     render_tile_sized(i, 100 + roundf(random_float() * 50), 1, 100);
    // }
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_end);


    
    // handle_stick_movement(&player, 2.0f);
    // sprintf(buffer, "D %.2f", player.angleInDegrees);
    // print_text(200, 100, buffer);
    // sprintf(buffer, "R %.5f", player.angleInDegrees* PI / 180.0f);
    // print_text(200, 120, buffer);
	// sprintf(buffer, "SR %.3f", sins(player.angleInDegrees* PI / 180.0f));
    // print_text(200, 140, buffer);
	// sprintf(buffer, "SD %.3f", sins(player.angleInDegrees* PI / 180.0f));
    // print_text(200, 160, buffer);
	// sprintf(buffer, "SA %.3f", sins(degrees_to_angle(player.angleInDegrees)));
    // print_text(200, 180, buffer);
	// sprintf(buffer, "FL %.3f", (f32)(s32)(player.angleInDegrees* PI / 180.0f));
    // print_text(200, 200, buffer);
	// sprintf(buffer, "sqrtf %.1f", sqrtf(player.angleInDegrees));
    // print_text(200, 220, buffer);



    


    // sprintf(buffer, "start %.1f %.1f", ray.hitPosition[0], ray.hitPosition[1]);
    // print_text(100, 20, buffer);
    // sprintf(buffer, "hit %d", ray.hit);
    // print_text(100, 40, buffer);



    //draw_background();




    // gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    // add_texture(0);
    // render_tile(playerPos[0], playerPos[2]);
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_end);

    //Enemy
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);
    // add_texture(3);
    // move_tile(&enemyPos, player.pos, 0.1f);
    // render_tile(enemyPos[0], enemyPos[2]);
    // gSPDisplayList(gDisplayListHead++, dl_hud_img_end);