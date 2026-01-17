#pragma once

#include <pebble.h>
#include "../pge.h"

#define PGE_TITLE_LENGTH_MAX 32
#define PGE_TITLE_ACTION_MAX 16

#define PGE_TITLE_HIGHSCORE_PS_KEY 3427

/**
 * Show a pre-built title page with your game title and the background resource
 * Use the button ID in the click handler to navigate the title screen
 */
void pge_title_push(const char *title, const char *up_action,
    const char *select_action, const char *down_action, 
    GColor title_color, int background_res_id, PGEClickHandler *click_handler);

/**
 * Hide and destroy the title page
 */
void pge_title_pop(void);

/**
 * Set a game-wide highscore
 */
void pge_title_set_highscore(int new_highscore);

/**
 * Get the last save highscore
 */
int pge_title_get_highscore(void);

void pge_title_show_highscore(bool show);
