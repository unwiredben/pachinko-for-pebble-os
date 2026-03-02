#pragma once

#include <pebble.h>

typedef int32_t Fixed16_16;
#define FIXED16_16_FROM_INT(i) ((i) << 16)
#define INT_FROM_FIXED16_16(f) (((f) + 32768) >> 16)

typedef struct Position {
  Fixed16_16 x;
  Fixed16_16 y;
} Position;

// velocity is defined as pixel motion per tick (1/30th of a second)
typedef struct Velocity {
  Fixed16_16 dx;
  Fixed16_16 dy;
} Velocity;

// Gravity: ~0.1 pixels per tick² (gentler acceleration for small display)
#define GRAVITY_DY (FIXED16_16_FROM_INT(1) / 10)
static Velocity s_gravity = {
  .dx = 0,
  .dy = GRAVITY_DY,
};

// a ball with position off screen and 0 velocity is considered inactive
typedef struct BallState {
  Position position;
  Velocity velocity;
} BallState;

inline void reset_ball(BallState *ball) {
  ball->position.x = FIXED16_16_FROM_INT(-50);
  ball->position.y = FIXED16_16_FROM_INT(-50);
  ball->velocity.dx = FIXED16_16_FROM_INT(0);
  ball->velocity.dy = FIXED16_16_FROM_INT(0);
}

inline void ball_tick(BallState *ball) {
  ball->position.x += ball->velocity.dx;
  ball->position.y += ball->velocity.dy;
}

inline void ball_apply_force(BallState *ball, Velocity force) {
  ball->velocity.dx += force.dx;
  ball->velocity.dy += force.dy;
}

inline void draw_ball(GContext *ctx, BallState *ball) {
  int16_t x = INT_FROM_FIXED16_16(ball->position.x);
  int16_t y = INT_FROM_FIXED16_16(ball->position.y);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(x, y), 3);
  graphics_draw_circle(ctx, GPoint(x, y), 3);
}
