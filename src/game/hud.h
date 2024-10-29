#ifndef HUD_H
#define HUD_H

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#define HUD_POWER_METER_X            140
#define HUD_POWER_METER_EMPHASIZED_Y 166
#define HUD_POWER_METER_Y            200
#define HUD_POWER_METER_HIDDEN_Y     300

#define HUD_TOP_Y 209
#define HUD_BOTTOM_Y 19 // Same height as the Lakitu icon

#define HUD_COINS_X 168
#define HUD_STARS_X 78 // This starts counting from the right edge
#define HUD_CAMERA_X 54 // This starts counting from the right edge

enum PowerMeterAnimation {
    POWER_METER_HIDDEN,
    POWER_METER_EMPHASIZED,
    POWER_METER_DEEMPHASIZING,
    POWER_METER_HIDING,
    POWER_METER_VISIBLE
};

#ifdef BREATH_METER
enum BreathMeterAnimation {
    BREATH_METER_HIDDEN,
    BREATH_METER_SHOWING,
    BREATH_METER_HIDING,
    BREATH_METER_VISIBLE
};
#endif

enum CameraHUDLUT {
    GLYPH_CAM_CAMERA,
    GLYPH_CAM_MARIO_HEAD,
    GLYPH_CAM_LAKITU_HEAD,
    GLYPH_CAM_FIXED,
    GLYPH_CAM_ARROW_UP,
    GLYPH_CAM_ARROW_DOWN
};

void set_hud_camera_status(s16 status);
void render_hud(void);

// ----- EDIT

struct Entity {
    s32 spriteIndex;
    Vec3f pos;
    f32 angleInDegrees;
};

struct Ray {
  Vec3f hitPosition;
  f32 distance;
  Bool8 hit;
};

void fooTest();
void draw_render_demo();

void draw_entity(struct Entity *entity);
void draw_background();
void add_texture(s8 glyphIndex);

void render_tile(s32 x, s32 y);
void render_tile_sized(s32 x, s32 y, s32 xSize, s32 ySize);
void render_tile_cords(s32 x, s32 y, s32 x2, s32 y2);

void move_tile(Vec3f *pos, Vec3f targetPos, f32 velocity);
void handle_stick_movement(struct Entity *entity, f32 velocity);

void castRay(Vec3f start, float angleInDegrees, const s32 grid[10][10], struct Ray *ray);


#endif // HUD_H
