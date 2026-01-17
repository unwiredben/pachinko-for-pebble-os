#pragma once

#include "pebble.h"
#include "../pge.h"  // GLine

// Convenience types
typedef struct GLine {
  GPoint p1;
  GPoint p2;
} GLine;

bool pge_collision_rectangle_rectangle(const GRect *rect_a, const GRect *rect_b);
bool pge_collision_line_rectangle(const GLine *line, const GRect *rect);
bool pge_collision_line_line(const GLine *line_a, const GLine *line_b);
bool pge_collision_point_rectangle(const GPoint *point, const GRect *rect);

