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

const char s_vibration_on[] = "Disable vibration";
const char s_vibration_off[] = "Enable vibration";

// forward declarations
static void change_vibration(int index, void *context);
static void show_help(int index, void *context);
static void show_high_scores(int index, void *context);
static void reset_ball_count(int index, void *context);

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

void change_vibration(int index, void *context) {
  s_vibration_enabled = !s_vibration_enabled;
  SimpleMenuItem *item = &s_options_items[index];
  if (s_vibration_enabled) {
    item->title = s_vibration_on;
  } else {
    item->title = s_vibration_off;
  }
  layer_mark_dirty(simple_menu_layer_get_layer(s_options_menu_layer));
}

void show_help(int index, void *context) {
  // FIXME: show scroller with help text
}

void show_high_scores(int index, void *context) {
  // FIXME: show high scores window
}

void reset_ball_count(int index, void *context) {
  set_ball_count(INITIAL_BALL_COUNT);
}

static void options_window_load(Window *window) {
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

static void show_options_window() {
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

// ------------------------------------------------

static Window *s_game_window;
static BitmapLayer *s_titlescreen_layer;
static GBitmap *s_titlescreen_bitmap;
static TextLayer *s_score_layer;
static char s_score_text[14] = "9999999 balls";
static Layer *s_pachinko_layer;
static AppTimer *s_render_timer = NULL;
static uint8_t s_framerate = 30;

#define BALL_RADIUS 3
#define MAX_BALLS 8
#define BORDER_RESTITUTION_NUM 3
#define BORDER_RESTITUTION_DEN 10
#define LAUNCH_VELOCITY_DX (-FIXED16_16_FROM_INT(6))
#define LAUNCH_VELOCITY_DY (-FIXED16_16_FROM_INT(2))
#define LAUNCH_VELOCITY_VARIATION_PERCENT 20
#define BOTTOM_CULL_MARGIN_PX 6
#define STUCK_SPEED_THRESHOLD FIXED16_16_FROM_INT(1)
#define BALL_COLLISION_RESTITUTION_NUM 9
#define BALL_COLLISION_RESTITUTION_DEN 10
static BallState s_balls[MAX_BALLS];
static bool s_ball_active[MAX_BALLS];

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

static void update_ball_physics(void) {
  GPoint center = get_playfield_center();
  int16_t radius = get_playfield_radius();
  int16_t bottom_limit = center.y + radius - BALL_RADIUS;
  int16_t stuck_cull_y = bottom_limit - BOTTOM_CULL_MARGIN_PX;
  GRect playfield_bounds = layer_get_bounds(s_pachinko_layer);

  for (int i = 0; i < MAX_BALLS; i++) {
    if (!s_ball_active[i]) {
      continue;
    }

    ball_apply_force(&s_balls[i], s_gravity);
    ball_tick(&s_balls[i]);
  }

  resolve_ball_ball_collisions();

  for (int i = 0; i < MAX_BALLS; i++) {
    if (!s_ball_active[i]) {
      continue;
    }

    int16_t ball_y = INT_FROM_FIXED16_16(s_balls[i].position.y);
    Fixed16_16 abs_dx = s_balls[i].velocity.dx < 0
      ? -s_balls[i].velocity.dx
      : s_balls[i].velocity.dx;
    Fixed16_16 abs_dy = s_balls[i].velocity.dy < 0
      ? -s_balls[i].velocity.dy
      : s_balls[i].velocity.dy;
    Fixed16_16 speed_l1 = abs_dx + abs_dy;

    if (ball_y > bottom_limit ||
        (ball_y >= stuck_cull_y && speed_l1 <= STUCK_SPEED_THRESHOLD)) {
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
}

static void frame_timer_handler(void *context) {
  if (s_game_state == GAME_STATE_PLAYING) {
    update_ball_physics();
    layer_mark_dirty(s_pachinko_layer);
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
    s_game_state = GAME_STATE_PLAYING;
    game_window_set_active_layers();
    return true;
  }
  return false;
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
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(rect.size.w / 2, rect.size.h / 2), radius);

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

  const int16_t text_y_offset = -2;
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

  // Register new Timer to begin frame rendering loop
  s_render_timer = app_timer_register(1000 / s_framerate, frame_timer_handler,
      NULL);
}

static void game_window_appear(Window *window) {
  game_window_set_active_layers();

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
