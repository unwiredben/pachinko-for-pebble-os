#include <pebble.h>

enum GameState {
  GAME_STATE_TITLESCREEN,
  GAME_STATE_PLAYING,
  GAME_STATE_OUT_OF_BALLS,
  GAME_STATE_OPTIONS,
};

static enum GameState s_game_state = GAME_STATE_TITLESCREEN;

static bool s_vibration_enabled = true;
#define INITIAL_BALL_COUNT 10
static uint32_t s_ball_count = INITIAL_BALL_COUNT;

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

static Velocity s_gravity = {
  .dx = FIXED16_16_FROM_INT(0),
  .dy = FIXED16_16_FROM_INT(1), // 1 pixel per tick downward
};

// a ball with position off screen and 0 velocity is considered inactive
typedef struct BallState {
  Position position;
  Velocity velocity;
} BallState;

void reset_ball(BallState *ball) {
  ball->position.x = FIXED16_16_FROM_INT(-50);
  ball->position.y = FIXED16_16_FROM_INT(-50);
  ball->velocity.dx = FIXED16_16_FROM_INT(0);
  ball->velocity.dy = FIXED16_16_FROM_INT(0);
}

void ball_tick(BallState *ball) {
  ball->position.x += ball->velocity.dx;
  ball->position.y += ball->velocity.dy;
}

void ball_apply_force(BallState *ball, Velocity force) {
  ball->velocity.dx += force.dx;
  ball->velocity.dy += force.dy;
}

void draw_ball(GContext *ctx, BallState *ball) {
  int16_t x = INT_FROM_FIXED16_16(ball->position.x);
  int16_t y = INT_FROM_FIXED16_16(ball->position.y);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(x, y), 3);
  graphics_draw_circle(ctx, GPoint(x, y), 3);
}

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
static void show_credits(int index, void *context);

SimpleMenuItem s_options_items[] = {
  {
    .title = s_vibration_on,
    .callback = change_vibration,
  },
  {
    .title = "How to Play",
    .callback = show_help,
  },
  {
    .title = "High Scores",
    .callback = show_high_scores,
  },
  {
    .title = "Reset ball count",
    .callback = reset_ball_count,
  },
  {
    .title = "Credits",
    .callback = show_credits,
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

void show_credits(int index, void *context) {
  // FIXME: show credits window
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
  s_options_window = window_create();
  window_set_window_handlers(s_options_window, (WindowHandlers) {
    .load = options_window_load,
    .unload = options_window_unload,
  });
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

static void toggle_auto_launch() {
  // Placeholder function to toggle auto-launch feature
}

static void launch_ball() {
  if (s_ball_count > 0) {
    set_ball_count(s_ball_count - 1);
  }
  // FIXME: actually put a ball into action
}

static void game_window_set_active_layers(void) {
  bool on_title_screen = (s_game_state == GAME_STATE_TITLESCREEN);
  layer_set_hidden(
    bitmap_layer_get_layer(s_titlescreen_layer), !on_title_screen);
  layer_set_hidden(
    text_layer_get_layer(s_score_layer), on_title_screen);
  layer_set_hidden(s_pachinko_layer, on_title_screen);
}

static void game_up_click_handler(ClickRecognizerRef ref, void *context) {
  toggle_auto_launch();
}

static void game_select_click_handler(ClickRecognizerRef ref, void *context) {
  if (s_game_state == GAME_STATE_TITLESCREEN) {
    s_game_state = GAME_STATE_PLAYING;
    game_window_set_active_layers();
  }
  else {
    show_options_window();
  }
}

static void game_down_click_handler(ClickRecognizerRef ref, void *context) {
  launch_ball();
}

static void update_pachinko_layer(Layer *layer, GContext *ctx) {
  // black background already drawn by windows
  GRect rect = layer_get_bounds(layer);
  int16_t radius = rect.size.w < rect.size.h
    ? rect.size.w / 2
    : rect.size.h / 2;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(rect.size.w / 2, rect.size.h / 2), radius);
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

  GFont score_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  GSize font_size = graphics_text_layout_get_content_size(
    s_score_text, score_font, bounds, GTextOverflowModeWordWrap,
    GTextAlignmentCenter);
  s_score_layer = text_layer_create(GRect(0, 0, bounds.size.w, font_size.h + 2));
  text_layer_set_text_color(s_score_layer, GColorWhite);
  text_layer_set_background_color(s_score_layer, GColorBlack);
  text_layer_set_font(s_score_layer, score_font);
  text_layer_set_text_alignment(s_score_layer, GTextAlignmentCenter);
  text_layer_set_text(s_score_layer, s_score_text);
  layer_add_child(window_layer, text_layer_get_layer(s_score_layer));
  
  bounds.origin.y += font_size.h + 2;
  bounds.size.h -= font_size.h + 2;

  s_pachinko_layer = layer_create(bounds);
  layer_set_update_proc(s_pachinko_layer, update_pachinko_layer);
  layer_add_child(window_layer, s_pachinko_layer);

  game_window_set_active_layers();
}

static void game_window_appear(Window *window) {
  // FIXME: set layer visibility based on game state
  // FIXME: setup update timer
}

static void game_window_disappear(Window *window) {
  // FIXME: stop update timer
}

static void game_window_unload(Window *window) {
  bitmap_layer_destroy(s_titlescreen_layer);
  s_titlescreen_layer = NULL;
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
  s_game_window = window_create();
  window_set_background_color(s_game_window, GColorBlack);
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
