#include <pebble.h>
#include <stdlib.h>

#include "ball.h"

enum GameState {
  GAME_STATE_TITLESCREEN,
  GAME_STATE_PLAYING,
  GAME_STATE_OUT_OF_BALLS,
  GAME_STATE_OPTIONS,
};

static enum GameState s_game_state = GAME_STATE_TITLESCREEN;

static bool s_vibration_enabled = true;
#define INITIAL_BALL_COUNT 100
static uint32_t s_ball_count = INITIAL_BALL_COUNT;

static void set_ball_count(uint16_t count);

static Window *s_options_window;
static SimpleMenuLayer *s_options_menu_layer;
static Window *s_layout_window;
static SimpleMenuLayer *s_layout_menu_layer;

const char s_vibration_on[] = "Disable vibration";
const char s_vibration_off[] = "Enable vibration";

// forward declarations
static void change_vibration(int index, void *context);
static void show_layout_menu(int index, void *context);
static void select_board_layout(int index, void *context);
static void show_help(int index, void *context);
static void show_high_scores(int index, void *context);
static void reset_ball_count(int index, void *context);
static int16_t get_active_board_layout_index(void);

SimpleMenuItem s_options_items[] = {
  {
    .title = s_vibration_on,
    .callback = change_vibration,
  },
  {
    .title = "Reset ball count",
    .callback = reset_ball_count,
  },
  {
    .title = "Board layout",
    .callback = show_layout_menu,
  },
  {
    .title = "High Scores",
    .callback = show_high_scores,
  },
  {
    .title = "How to Play",
    .callback = show_help,
  },
};

SimpleMenuSection s_options_section[] = {
  {
    .num_items = ARRAY_LENGTH(s_options_items),
    .items = s_options_items,
  },
};

static void sync_vibration_menu_item_title(void) {
  s_options_items[0].title = s_vibration_enabled ? s_vibration_on : s_vibration_off;
}

SimpleMenuItem s_layout_items[] = {
  {
    .title = "Dense",
    .callback = select_board_layout,
  },
  {
    .title = "Classic",
    .callback = select_board_layout,
  },
};

SimpleMenuSection s_layout_section[] = {
  {
    .title = "Board Layout",
    .num_items = ARRAY_LENGTH(s_layout_items),
    .items = s_layout_items,
  },
};

static void return_to_game_from_options(void) {
  if (s_layout_window && window_stack_contains_window(s_layout_window)) {
    window_stack_remove(s_layout_window, true /* animated */);
  }
  if (s_options_window && window_stack_contains_window(s_options_window)) {
    window_stack_remove(s_options_window, true /* animated */);
  }
}

void change_vibration(int index, void *context) {
  if (index < 0 || index >= (int)ARRAY_LENGTH(s_options_items)) {
    return;
  }

  s_vibration_enabled = !s_vibration_enabled;
  if (s_vibration_enabled) {
    vibes_short_pulse();
  }
  sync_vibration_menu_item_title();
  if (s_options_menu_layer) {
    layer_mark_dirty(simple_menu_layer_get_layer(s_options_menu_layer));
  }
  return_to_game_from_options();
}

void show_help(int index, void *context) {
  // FIXME: show scroller with help text
}

void show_high_scores(int index, void *context) {
  // FIXME: show high scores window
}

void reset_ball_count(int index, void *context) {
  set_ball_count(INITIAL_BALL_COUNT);
  return_to_game_from_options();
}

static void options_window_load(Window *window) {
  sync_vibration_menu_item_title();
  Layer* layer = window_get_root_layer(s_options_window);
  GRect bounds = layer_get_bounds(layer);
  s_options_menu_layer = simple_menu_layer_create(
    bounds, window, s_options_section, ARRAY_LENGTH(s_options_section), NULL);
  layer_add_child(layer, simple_menu_layer_get_layer(s_options_menu_layer));
 }

static void options_window_unload(Window *window) {
  simple_menu_layer_destroy(s_options_menu_layer);
  s_options_menu_layer = NULL;

  if (s_ball_count == 0) {
    s_game_state = GAME_STATE_OUT_OF_BALLS;
  }
  else {
    s_game_state = GAME_STATE_PLAYING;
  }
}

static void layout_window_load(Window *window) {
  Layer* layer = window_get_root_layer(s_layout_window);
  GRect bounds = layer_get_bounds(layer);
  s_layout_menu_layer = simple_menu_layer_create(
    bounds, window, s_layout_section, ARRAY_LENGTH(s_layout_section), NULL);
  menu_layer_set_selected_index(
    simple_menu_layer_get_menu_layer(s_layout_menu_layer),
    (MenuIndex) {.section = 0, .row = get_active_board_layout_index()},
    MenuRowAlignCenter,
    false);
  layer_add_child(layer, simple_menu_layer_get_layer(s_layout_menu_layer));
}

static void layout_window_unload(Window *window) {
  simple_menu_layer_destroy(s_layout_menu_layer);
  s_layout_menu_layer = NULL;
}

static void show_options_window() {
  sync_vibration_menu_item_title();
  if (!s_options_window) {
    s_options_window = window_create();
    window_set_window_handlers(s_options_window, (WindowHandlers) {
      .load = options_window_load,
      .unload = options_window_unload,
    });
  }
  window_stack_push(s_options_window, true /* animated */);
  s_game_state = GAME_STATE_OPTIONS;
}

static void show_layout_menu(int index, void *context) {
  if (index < 0 || index >= (int)ARRAY_LENGTH(s_options_items)) {
    return;
  }

  if (!s_layout_window) {
    s_layout_window = window_create();
    window_set_window_handlers(s_layout_window, (WindowHandlers) {
      .load = layout_window_load,
      .unload = layout_window_unload,
    });
  }

  window_stack_push(s_layout_window, true /* animated */);
}

// ------------------------------------------------

static Window *s_game_window;
static BitmapLayer *s_titlescreen_layer;
static GBitmap *s_titlescreen_bitmap;
static TextLayer *s_score_layer;
static char s_score_text[14] = "9999999 balls";
static Layer *s_pachinko_layer;
static AppTimer *s_render_timer = NULL;
static AppTimer *s_titlescreen_timer = NULL;
static uint8_t s_framerate = 30;
static bool s_collision_occurred_this_frame = false;
static uint32_t s_collision_vibe_durations[] = {20};
static const VibePattern s_collision_vibe_pattern = {
  .durations = s_collision_vibe_durations,
  .num_segments = 1,
};

#define TITLESCREEN_AUTODISMISS_DELAY_MS 3000

#define BALL_RADIUS 3
#define BORDER_RESTITUTION_NUM 3
#define BORDER_RESTITUTION_DEN 10
#define LAUNCH_VELOCITY_DX (-FIXED16_16_FROM_INT(6))
#define LAUNCH_VELOCITY_DY (-FIXED16_16_FROM_INT(2))
#define LAUNCH_VELOCITY_VARIATION_PERCENT 20
#define BOTTOM_CULL_MARGIN_PX 8
#define STUCK_SPEED_THRESHOLD FIXED16_16_FROM_INT(1)
#define BALL_COLLISION_RESTITUTION_NUM 9
#define BALL_COLLISION_RESTITUTION_DEN 10
#define PIN_SIZE_PX 2
#define PIN_COLLISION_RADIUS (BALL_RADIUS + 1)
#define PIN_RESTITUTION_NUM 2
#define PIN_RESTITUTION_DEN 10
#define OUTER_DEFLECTOR_MARGIN_PX 1
#define OUTER_DEFLECTOR_ANGLE_DEG 25
#define Q10_SCALE 1024
// Angle is measured clockwise from top, so x uses sin(theta), y uses cos(theta).
#define OUTER_DEFLECTOR_COS_Q10 ((Q10_SCALE * 4226 + 5000) / 10000) // sin(25 deg)
#define OUTER_DEFLECTOR_SIN_Q10 ((Q10_SCALE * 9063 + 5000) / 10000) // cos(25 deg)

#define MAX_BALLS 8
static BallState s_balls[MAX_BALLS];
static bool s_ball_active[MAX_BALLS];

typedef struct PinLayoutPosition {
  int16_t x_q10;
  int16_t y_q10;
} PinLayoutPosition;

typedef struct PinLayout {
  const PinLayoutPosition *positions;
  uint8_t count;
} PinLayout;

#define Q10_FROM_RATIO(num, den) ((int16_t)(((num) * 1024) / (den)))

// Layouts are defined in normalized coordinates where 1024 == playfield radius.
static const PinLayoutPosition s_dense_pin_layout_positions[] = {
  {Q10_FROM_RATIO(-5, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(-3, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(-1, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(1, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(3, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(5, 8), Q10_FROM_RATIO(-1, 2)},

  {Q10_FROM_RATIO(-6, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(-4, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(-2, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(0, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(2, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(4, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(6, 8), Q10_FROM_RATIO(-3, 14)},

  {Q10_FROM_RATIO(-7, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(-5, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(-3, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(-1, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(1, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(3, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(5, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(7, 8), Q10_FROM_RATIO(1, 14)},

  {Q10_FROM_RATIO(-6, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(-4, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(-2, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(0, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(2, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(4, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(6, 8), Q10_FROM_RATIO(5, 14)},

  {Q10_FROM_RATIO(-5, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(-3, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(-1, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(1, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(3, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(5, 8), Q10_FROM_RATIO(9, 14)},
};

static const PinLayoutPosition s_classic_pin_layout_positions[] = {
  {Q10_FROM_RATIO(-4, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(-2, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(0, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(2, 8), Q10_FROM_RATIO(-1, 2)},
  {Q10_FROM_RATIO(4, 8), Q10_FROM_RATIO(-1, 2)},

  {Q10_FROM_RATIO(-5, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(-3, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(-1, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(1, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(3, 8), Q10_FROM_RATIO(-3, 14)},
  {Q10_FROM_RATIO(5, 8), Q10_FROM_RATIO(-3, 14)},

  {Q10_FROM_RATIO(-6, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(-4, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(-2, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(0, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(2, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(4, 8), Q10_FROM_RATIO(1, 14)},
  {Q10_FROM_RATIO(6, 8), Q10_FROM_RATIO(1, 14)},

  {Q10_FROM_RATIO(-5, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(-3, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(-1, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(1, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(3, 8), Q10_FROM_RATIO(5, 14)},
  {Q10_FROM_RATIO(5, 8), Q10_FROM_RATIO(5, 14)},

  {Q10_FROM_RATIO(-4, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(-2, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(0, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(2, 8), Q10_FROM_RATIO(9, 14)},
  {Q10_FROM_RATIO(4, 8), Q10_FROM_RATIO(9, 14)},
};

static const PinLayout s_dense_pin_layout = {
  .positions = s_dense_pin_layout_positions,
  .count = ARRAY_LENGTH(s_dense_pin_layout_positions),
};

static const PinLayout s_classic_pin_layout = {
  .positions = s_classic_pin_layout_positions,
  .count = ARRAY_LENGTH(s_classic_pin_layout_positions),
};

enum BoardLayout {
  BOARD_LAYOUT_DENSE,
  BOARD_LAYOUT_CLASSIC,
};

static const PinLayout *s_pin_layouts[] = {
  [BOARD_LAYOUT_DENSE] = &s_dense_pin_layout,
  [BOARD_LAYOUT_CLASSIC] = &s_classic_pin_layout,
};

// Switch this value to change which static pin layout is active.
static enum BoardLayout s_active_board_layout = BOARD_LAYOUT_DENSE;

static const PinLayout *get_active_pin_layout(void) {
  return s_pin_layouts[s_active_board_layout];
}

static int16_t get_active_board_layout_index(void) {
  return (int16_t)s_active_board_layout;
}

static void select_board_layout(int index, void *context) {
  if (index < 0 || index >= (int)ARRAY_LENGTH(s_layout_items)) {
    return;
  }

  s_active_board_layout = (enum BoardLayout)index;

  if (s_options_menu_layer) {
    layer_mark_dirty(simple_menu_layer_get_layer(s_options_menu_layer));
  }
  if (s_pachinko_layer) {
    layer_mark_dirty(s_pachinko_layer);
  }
  return_to_game_from_options();
}

static Fixed16_16 vary_launch_velocity(Fixed16_16 base_velocity) {
  // Scale launch speed by 80%..120% for slight per-ball variation.
  int32_t scale_percent = 100 + ((rand() %
      (LAUNCH_VELOCITY_VARIATION_PERCENT * 2 + 1)) -
      LAUNCH_VELOCITY_VARIATION_PERCENT);
  return (base_velocity * scale_percent) / 100;
}

static int32_t isqrt32(int32_t value) {
  if (value <= 0) {
    return 0;
  }

  uint32_t n = (uint32_t)value;
  uint32_t result = 0;
  uint32_t bit = 1u << 30;

  while (bit > n) {
    bit >>= 2;
  }

  while (bit != 0) {
    if (n >= result + bit) {
      n -= result + bit;
      result = (result >> 1) + bit;
    } else {
      result >>= 1;
    }
    bit >>= 2;
  }

  return (int32_t)result;
}

static void resolve_circle_border_collision(BallState *ball, GPoint center,
    int16_t playfield_radius) {
  const int16_t collision_radius = playfield_radius - BALL_RADIUS;
  int16_t x = INT_FROM_FIXED16_16(ball->position.x);
  int16_t y = INT_FROM_FIXED16_16(ball->position.y);

  int32_t rel_x = x - center.x;
  int32_t rel_y = y - center.y;
  int32_t dist_sq = rel_x * rel_x + rel_y * rel_y;
  int32_t radius_sq = collision_radius * collision_radius;

  if (dist_sq <= radius_sq) {
    return;
  }

  int32_t distance = isqrt32(dist_sq);
  if (distance <= 0) {
    return;
  }

  // Project the ball back inside the circular playfield.
  rel_x = (rel_x * collision_radius) / distance;
  rel_y = (rel_y * collision_radius) / distance;
  x = center.x + rel_x;
  y = center.y + rel_y;
  ball->position.x = FIXED16_16_FROM_INT(x);
  ball->position.y = FIXED16_16_FROM_INT(y);

  // Compute a unit normal in Q10 so we can remove outward velocity.
  int32_t normal_x_q10 = (rel_x * 1024) / collision_radius;
  int32_t normal_y_q10 = (rel_y * 1024) / collision_radius;
  int32_t normal_velocity =
    (ball->velocity.dx * normal_x_q10 + ball->velocity.dy * normal_y_q10) / 1024;

  if (normal_velocity > 0) {
    int32_t reflect_scale = ((1024 + (1024 * BORDER_RESTITUTION_NUM / BORDER_RESTITUTION_DEN))
        * normal_velocity) / 1024;
    ball->velocity.dx -= (reflect_scale * normal_x_q10) / 1024;
    ball->velocity.dy -= (reflect_scale * normal_y_q10) / 1024;
  }
}

static int16_t get_playfield_radius(void) {
  GRect rect = layer_get_bounds(s_pachinko_layer);
  return (rect.size.w < rect.size.h ? rect.size.w : rect.size.h) / 2;
}

static GPoint get_playfield_center(void) {
  GRect rect = layer_get_bounds(s_pachinko_layer);
  return GPoint(rect.size.w / 2, rect.size.h / 2);
}

static bool should_cull_offscreen_ball(const BallState *ball, GRect bounds) {
  int32_t x = INT_FROM_FIXED16_16(ball->position.x);
  int32_t y = INT_FROM_FIXED16_16(ball->position.y);
  int32_t next_x = x + INT_FROM_FIXED16_16(ball->velocity.dx);
  int32_t next_y = y + INT_FROM_FIXED16_16(ball->velocity.dy);
  int32_t left = -BALL_RADIUS;
  int32_t right = bounds.size.w + BALL_RADIUS;
  int32_t top = -BALL_RADIUS;
  int32_t bottom = bounds.size.h + BALL_RADIUS;

  return x < left || x > right || y < top || y > bottom ||
    next_x < left || next_x > right || next_y < top || next_y > bottom;
}

static GPoint get_layout_pin_position(const PinLayoutPosition *layout_position,
    GPoint center, int16_t radius) {
  int16_t x = center.x + (radius * layout_position->x_q10) / 1024;
  int16_t y = center.y + (radius * layout_position->y_q10) / 1024;
  return GPoint(x, y);
}

static GPoint get_outer_deflector_pin_position(GPoint center, int16_t radius) {
  int16_t ring_radius = radius - OUTER_DEFLECTOR_MARGIN_PX;
  if (ring_radius < BALL_RADIUS + 2) {
    ring_radius = BALL_RADIUS + 2;
  }

  // Place one pin around 25 degrees clockwise from the top of the ring.
  int16_t x = center.x + (ring_radius * OUTER_DEFLECTOR_COS_Q10) / 1024;
  int16_t y = center.y - (ring_radius * OUTER_DEFLECTOR_SIN_Q10) / 1024;
  return GPoint(x, y);
}

static bool segment_intersects_pin_area(GPoint start, GPoint end, GPoint pin,
    int32_t collision_radius_sq, GPoint *closest_point) {
  int32_t start_dx = start.x - pin.x;
  int32_t start_dy = start.y - pin.y;
  int32_t start_dist_sq = start_dx * start_dx + start_dy * start_dy;
  if (start_dist_sq < collision_radius_sq) {
    *closest_point = start;
    return true;
  }

  int32_t end_dx = end.x - pin.x;
  int32_t end_dy = end.y - pin.y;
  int32_t end_dist_sq = end_dx * end_dx + end_dy * end_dy;
  if (end_dist_sq < collision_radius_sq) {
    *closest_point = end;
    return true;
  }

  int32_t seg_dx = end.x - start.x;
  int32_t seg_dy = end.y - start.y;
  int64_t seg_len_sq = (int64_t)seg_dx * seg_dx + (int64_t)seg_dy * seg_dy;
  if (seg_len_sq <= 0) {
    return false;
  }

  int64_t t_num = -((int64_t)start_dx * seg_dx + (int64_t)start_dy * seg_dy);
  if (t_num < 0) {
    t_num = 0;
  } else if (t_num > seg_len_sq) {
    t_num = seg_len_sq;
  }

  int32_t closest_x = start.x + (int32_t)((seg_dx * t_num) / seg_len_sq);
  int32_t closest_y = start.y + (int32_t)((seg_dy * t_num) / seg_len_sq);
  int32_t close_dx = closest_x - pin.x;
  int32_t close_dy = closest_y - pin.y;
  int32_t close_dist_sq = close_dx * close_dx + close_dy * close_dy;
  if (close_dist_sq < collision_radius_sq) {
    *closest_point = GPoint(closest_x, closest_y);
    return true;
  }

  return false;
}

static void resolve_ball_single_pin_collision(BallState *ball,
    GPoint previous_position, GPoint pin) {
  const int32_t collision_radius = PIN_COLLISION_RADIUS;
  const int32_t collision_radius_sq = collision_radius * collision_radius;
  int32_t ball_x = INT_FROM_FIXED16_16(ball->position.x);
  int32_t ball_y = INT_FROM_FIXED16_16(ball->position.y);
  GPoint current_position = GPoint(ball_x, ball_y);

  GPoint closest_point = current_position;
  bool swept_hit = segment_intersects_pin_area(previous_position,
      current_position, pin, collision_radius_sq, &closest_point);

  int32_t diff_x = ball_x - pin.x;
  int32_t diff_y = ball_y - pin.y;
  int32_t dist_sq = diff_x * diff_x + diff_y * diff_y;
  bool overlap = dist_sq < collision_radius_sq;
  if (!overlap && !swept_hit) {
    return;
  }

  s_collision_occurred_this_frame = true;

  if (!overlap && swept_hit) {
    diff_x = closest_point.x - pin.x;
    diff_y = closest_point.y - pin.y;
    dist_sq = diff_x * diff_x + diff_y * diff_y;
  }

  int32_t distance = isqrt32(dist_sq);
  if (distance <= 0) {
    diff_x = 0;
    diff_y = -1;
    distance = 1;
  }

  int32_t normal_x_q10 = (diff_x * 1024) / distance;
  int32_t normal_y_q10 = (diff_y * 1024) / distance;

  if (overlap) {
    int32_t penetration = collision_radius - distance;
    int32_t move_x_q10 = normal_x_q10 * penetration;
    int32_t move_y_q10 = normal_y_q10 * penetration;

    // Convert Q10 displacement to Q16.16 by scaling with 2^(16-10)=64.
    ball->position.x += move_x_q10 * 64;
    ball->position.y += move_y_q10 * 64;
    ball_x = INT_FROM_FIXED16_16(ball->position.x);
    ball_y = INT_FROM_FIXED16_16(ball->position.y);

    // Recompute normal after positional correction.
    diff_x = ball_x - pin.x;
    diff_y = ball_y - pin.y;
    int32_t corrected_dist_sq = diff_x * diff_x + diff_y * diff_y;
    distance = isqrt32(corrected_dist_sq);
    if (distance <= 0) {
      distance = 1;
    }
    normal_x_q10 = (diff_x * 1024) / distance;
    normal_y_q10 = (diff_y * 1024) / distance;
  } else {
    // Move the ball to the pin boundary at the closest swept-contact point.
    int32_t resolved_x = pin.x + (normal_x_q10 * collision_radius) / 1024;
    int32_t resolved_y = pin.y + (normal_y_q10 * collision_radius) / 1024;
    ball->position.x = FIXED16_16_FROM_INT(resolved_x);
    ball->position.y = FIXED16_16_FROM_INT(resolved_y);
  }

  int32_t normal_velocity =
    (ball->velocity.dx * normal_x_q10 + ball->velocity.dy * normal_y_q10) / 1024;
  if (swept_hit && normal_velocity > 0) {
    normal_velocity = -normal_velocity;
  }
  if (normal_velocity < 0) {
    int32_t reflect_scale = ((1024 +
        (1024 * PIN_RESTITUTION_NUM / PIN_RESTITUTION_DEN)) *
        normal_velocity) / 1024;
    ball->velocity.dx -= (reflect_scale * normal_x_q10) / 1024;
    ball->velocity.dy -= (reflect_scale * normal_y_q10) / 1024;
  }
}

static void resolve_ball_pin_collisions(BallState *ball, GPoint previous_position,
    GPoint center, int16_t radius) {
  const PinLayout *active_pin_layout = get_active_pin_layout();
  for (uint8_t i = 0; i < active_pin_layout->count; i++) {
    GPoint pin = get_layout_pin_position(&active_pin_layout->positions[i],
        center, radius);
    resolve_ball_single_pin_collision(ball, previous_position, pin);
  }

  GPoint deflector_pin = get_outer_deflector_pin_position(center, radius);
  resolve_ball_single_pin_collision(ball, previous_position, deflector_pin);
}

static void resolve_ball_ball_collisions(void) {
  const int16_t min_distance = BALL_RADIUS * 2;
  const int32_t min_distance_sq = min_distance * min_distance;

  for (int i = 0; i < MAX_BALLS; i++) {
    if (!s_ball_active[i]) {
      continue;
    }

    for (int j = i + 1; j < MAX_BALLS; j++) {
      if (!s_ball_active[j]) {
        continue;
      }

      int32_t x_i = INT_FROM_FIXED16_16(s_balls[i].position.x);
      int32_t y_i = INT_FROM_FIXED16_16(s_balls[i].position.y);
      int32_t x_j = INT_FROM_FIXED16_16(s_balls[j].position.x);
      int32_t y_j = INT_FROM_FIXED16_16(s_balls[j].position.y);

      int32_t diff_x = x_i - x_j;
      int32_t diff_y = y_i - y_j;
      int32_t dist_sq = diff_x * diff_x + diff_y * diff_y;

      if (dist_sq >= min_distance_sq) {
        continue;
      }

      s_collision_occurred_this_frame = true;

      int32_t distance = isqrt32(dist_sq);
      if (distance == 0) {
        diff_x = min_distance;
        diff_y = 0;
        distance = min_distance;
      }

      int32_t normal_x_q10 = (diff_x * 1024) / distance;
      int32_t normal_y_q10 = (diff_y * 1024) / distance;

      int32_t penetration = min_distance - distance;
      if (penetration > 0) {
        int32_t move_q10 = (penetration * 1024) / 2;
        int32_t move_x = (normal_x_q10 * move_q10) / 1024;
        int32_t move_y = (normal_y_q10 * move_q10) / 1024;

        // Convert Q10 separation to Q16.16 by scaling with 2^(16-10)=64.
        s_balls[i].position.x += move_x * 64;
        s_balls[i].position.y += move_y * 64;
        s_balls[j].position.x -= move_x * 64;
        s_balls[j].position.y -= move_y * 64;
      }

      Fixed16_16 rel_velocity_normal =
        ((s_balls[i].velocity.dx - s_balls[j].velocity.dx) * normal_x_q10 +
        (s_balls[i].velocity.dy - s_balls[j].velocity.dy) * normal_y_q10) / 1024;

      if (rel_velocity_normal >= 0) {
        continue;
      }

      Fixed16_16 impulse = -(
        (rel_velocity_normal *
          (BALL_COLLISION_RESTITUTION_DEN + BALL_COLLISION_RESTITUTION_NUM)) /
        BALL_COLLISION_RESTITUTION_DEN) / 2;

      Fixed16_16 impulse_x = (impulse * normal_x_q10) / 1024;
      Fixed16_16 impulse_y = (impulse * normal_y_q10) / 1024;

      s_balls[i].velocity.dx += impulse_x;
      s_balls[i].velocity.dy += impulse_y;
      s_balls[j].velocity.dx -= impulse_x;
      s_balls[j].velocity.dy -= impulse_y;
    }
  }
}

static bool has_active_balls(void) {
  for (int i = 0; i < MAX_BALLS; i++) {
    if (s_ball_active[i]) {
      return true;
    }
  }
  return false;
}

static void update_ball_physics(void) {
  s_collision_occurred_this_frame = false;

  GPoint center = get_playfield_center();
  int16_t radius = get_playfield_radius();
  int16_t bottom_limit = center.y + radius - BALL_RADIUS;
  int16_t catch_band_y = bottom_limit - BOTTOM_CULL_MARGIN_PX;
  GRect playfield_bounds = layer_get_bounds(s_pachinko_layer);
  GPoint previous_positions[MAX_BALLS];

  for (int i = 0; i < MAX_BALLS; i++) {
    if (!s_ball_active[i]) {
      continue;
    }

    previous_positions[i] = GPoint(
      INT_FROM_FIXED16_16(s_balls[i].position.x),
      INT_FROM_FIXED16_16(s_balls[i].position.y));

    ball_apply_force(&s_balls[i], s_gravity);
    ball_tick(&s_balls[i]);
  }

  resolve_ball_ball_collisions();

  for (int i = 0; i < MAX_BALLS; i++) {
    if (!s_ball_active[i]) {
      continue;
    }

    resolve_ball_pin_collisions(&s_balls[i], previous_positions[i], center, radius);

    int16_t ball_y = INT_FROM_FIXED16_16(s_balls[i].position.y);

    if (ball_y >= catch_band_y && s_balls[i].velocity.dy > 0) {
      reset_ball(&s_balls[i]);
      s_ball_active[i] = false;
      continue;
    }

    resolve_circle_border_collision(&s_balls[i], center, radius);

    if (should_cull_offscreen_ball(&s_balls[i], playfield_bounds)) {
      reset_ball(&s_balls[i]);
      s_ball_active[i] = false;
      continue;
    }
  }

  if (s_collision_occurred_this_frame && s_vibration_enabled) {
    vibes_enqueue_custom_pattern(s_collision_vibe_pattern);
  }
}

static void frame_timer_handler(void *context) {
  if (s_game_state == GAME_STATE_PLAYING) {
    update_ball_physics();
    layer_mark_dirty(s_pachinko_layer);

    if (!has_active_balls()) {
      s_render_timer = NULL;
      return;
    }
  }
  s_render_timer = app_timer_register(1000 / s_framerate, frame_timer_handler, NULL);
}


static void toggle_auto_launch() {
  // Placeholder function to toggle auto-launch feature
}

static void launch_ball() {
  if (s_ball_count == 0) return;

  int slot = -1;
  for (int i = 0; i < MAX_BALLS; i++) {
    if (!s_ball_active[i]) {
      slot = i;
      break;
    }
  }
  if (slot < 0) return;

  set_ball_count(s_ball_count - 1);

  GPoint center = get_playfield_center();
  int16_t radius = get_playfield_radius();

  s_balls[slot].position.x = FIXED16_16_FROM_INT(center.x);
  s_balls[slot].position.y = FIXED16_16_FROM_INT(center.y + radius - BALL_RADIUS - 1);
  s_balls[slot].velocity.dx = vary_launch_velocity(LAUNCH_VELOCITY_DX);
  s_balls[slot].velocity.dy = vary_launch_velocity(LAUNCH_VELOCITY_DY);
  s_ball_active[slot] = true;

  if (s_render_timer == NULL && s_game_state == GAME_STATE_PLAYING) {
    s_render_timer = app_timer_register(1000 / s_framerate, frame_timer_handler, NULL);
  }
}

static void game_window_set_active_layers(void) {
  bool on_title_screen = (s_game_state == GAME_STATE_TITLESCREEN);
  layer_set_hidden(
    bitmap_layer_get_layer(s_titlescreen_layer), !on_title_screen);
  layer_set_hidden(
    text_layer_get_layer(s_score_layer), on_title_screen);
  layer_set_hidden(s_pachinko_layer, on_title_screen);
}

static bool dismiss_title_screen(void) {
  if (s_game_state == GAME_STATE_TITLESCREEN) {
    if (s_titlescreen_timer != NULL) {
      app_timer_cancel(s_titlescreen_timer);
      s_titlescreen_timer = NULL;
    }
    s_game_state = GAME_STATE_PLAYING;
    game_window_set_active_layers();
    return true;
  }
  return false;
}

static void titlescreen_timer_handler(void *context) {
  s_titlescreen_timer = NULL;
  dismiss_title_screen();
}

static void schedule_titlescreen_autodismiss(void) {
  if (s_game_state != GAME_STATE_TITLESCREEN || s_titlescreen_timer != NULL) {
    return;
  }

  s_titlescreen_timer = app_timer_register(TITLESCREEN_AUTODISMISS_DELAY_MS,
      titlescreen_timer_handler, NULL);
}

static void game_up_click_handler(ClickRecognizerRef ref, void *context) {
  if (dismiss_title_screen()) return;
  toggle_auto_launch();
}

static void game_select_click_handler(ClickRecognizerRef ref, void *context) {
  if (dismiss_title_screen()) return;
  show_options_window();
}

static void game_down_click_handler(ClickRecognizerRef ref, void *context) {
  if (dismiss_title_screen()) return;
  launch_ball();
}

static void update_pachinko_layer(Layer *layer, GContext *ctx) {
  GRect rect = layer_get_bounds(layer);
  int16_t radius = rect.size.w < rect.size.h
    ? rect.size.w / 2
    : rect.size.h / 2;
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
#endif
  graphics_fill_rect(ctx, rect, 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(rect.size.w / 2, rect.size.h / 2), radius);

  GPoint center = GPoint(rect.size.w / 2, rect.size.h / 2);
  graphics_context_set_fill_color(ctx, GColorWhite);
  const PinLayout *active_pin_layout = get_active_pin_layout();
  for (uint8_t i = 0; i < active_pin_layout->count; i++) {
    GPoint pin = get_layout_pin_position(&active_pin_layout->positions[i],
        center, radius);
    graphics_fill_rect(ctx, GRect(pin.x - 1, pin.y - 1, PIN_SIZE_PX, PIN_SIZE_PX),
        0, GCornerNone);
  }

  GPoint deflector_pin = get_outer_deflector_pin_position(center, radius);
  graphics_fill_rect(ctx,
      GRect(deflector_pin.x - 1, deflector_pin.y - 1, PIN_SIZE_PX, PIN_SIZE_PX),
      0, GCornerNone);

  for (int i = 0; i < MAX_BALLS; i++) {
    if (s_ball_active[i]) {
      draw_ball(ctx, &s_balls[i]);
    }
  }
}

#define LAUNCH_REPEAT_DELAY_MS 500

static void game_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, game_select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, game_up_click_handler);
  window_single_repeating_click_subscribe(
    BUTTON_ID_DOWN, LAUNCH_REPEAT_DELAY_MS, game_down_click_handler);
}

static void game_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_titlescreen_layer = bitmap_layer_create(bounds);
  s_titlescreen_bitmap =
    gbitmap_create_with_resource(RESOURCE_ID_TITLESCREEN_IMAGE);
  bitmap_layer_set_bitmap(s_titlescreen_layer, s_titlescreen_bitmap);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_titlescreen_layer));

  GFont score_font = fonts_get_system_font(FONT_KEY_GOTHIC_28);

  const int16_t text_y_offset = -8;
  const int16_t y_padding = 4;
  GSize font_size = graphics_text_layout_get_content_size(
    s_score_text, score_font, bounds, GTextOverflowModeWordWrap,
    GTextAlignmentCenter);
  s_score_layer = text_layer_create(GRect(0, text_y_offset,
      bounds.size.w, font_size.h));
  text_layer_set_text_color(s_score_layer, GColorBlack);
  text_layer_set_background_color(s_score_layer, GColorWhite);
  text_layer_set_font(s_score_layer, score_font);
  text_layer_set_text_alignment(s_score_layer, GTextAlignmentCenter);
  text_layer_set_text(s_score_layer, s_score_text);
  layer_add_child(window_layer, text_layer_get_layer(s_score_layer));

  bounds.origin.y += font_size.h + text_y_offset + y_padding;
  bounds.size.h -= font_size.h + text_y_offset + y_padding;

  s_pachinko_layer = layer_create(bounds);
  layer_set_update_proc(s_pachinko_layer, update_pachinko_layer);
  layer_add_child(window_layer, s_pachinko_layer);

  game_window_set_active_layers();
  schedule_titlescreen_autodismiss();

  // Register new Timer to begin frame rendering loop
  s_render_timer = app_timer_register(1000 / s_framerate, frame_timer_handler,
      NULL);
}

static void game_window_appear(Window *window) {
  game_window_set_active_layers();
  schedule_titlescreen_autodismiss();

  if (s_render_timer == NULL) {
    s_render_timer = app_timer_register(1000 / s_framerate, frame_timer_handler, NULL);
  }
}

static void game_window_disappear(Window *window) {
  // Stop the game
  if(s_render_timer != NULL) {
    app_timer_cancel(s_render_timer);
    s_render_timer = NULL;
  }

  if (s_titlescreen_timer != NULL) {
    app_timer_cancel(s_titlescreen_timer);
    s_titlescreen_timer = NULL;
  }
}

static void game_window_unload(Window *window) {
  bitmap_layer_destroy(s_titlescreen_layer);
  s_titlescreen_layer = NULL;
  gbitmap_destroy(s_titlescreen_bitmap);
  s_titlescreen_bitmap = NULL;
  text_layer_destroy(s_score_layer);
  s_score_layer = NULL;
  layer_destroy(s_pachinko_layer);
  s_pachinko_layer = NULL;
}

static void set_ball_count(uint16_t count) {
  s_ball_count = count;
  snprintf(s_score_text, sizeof(s_score_text), "%lu balls", s_ball_count);
  if (s_score_layer) {
    layer_mark_dirty(text_layer_get_layer(s_score_layer));
  }
}

static void init(void) {
  srand(time(NULL));
  s_game_window = window_create();
  window_set_background_color(s_game_window, GColorWhite);
  window_set_click_config_provider(s_game_window, game_click_config_provider);
  window_set_window_handlers(s_game_window, (WindowHandlers) {
    .load = game_window_load,
    .appear = game_window_appear,
    .disappear = game_window_disappear,
    .unload = game_window_unload,
  });
  window_stack_push(s_game_window, true /* animated */);
  set_ball_count(INITIAL_BALL_COUNT);
}

static void deinit(void) {
  if (s_layout_window) {
    window_destroy(s_layout_window);
    s_layout_window = NULL;
  }
  if (s_options_window) {
    window_destroy(s_options_window);
    s_options_window = NULL;
  }
  window_destroy(s_game_window);
  s_game_window = NULL;
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
