Pachinko for Pebble
===================

This is a simple Pachinko simulator that runs on Pebble OS.
It simulates the popular ball launching game from Japan, letting
the user switch between manual and automatic ball launching,
while affecting the movement using the watches' accelerometer.

It was developed in early 2026 by Ben Combee with graphical
assets and playtesting by Elias Combee.

User Interface Flow
===================

# Screens

## Title Screen
- shows title card and "The Life Unwired" games logo
- any button transitions to *Game Screen*

## Game Screen
- shows current and max ball count at top
- has round playfield with ball channel, pins, score areas, animations
- up button toggles automatic ball launching - 1 ball per second (?)
- automatic launching is off on entry to this screen and has to be turned back on
- select button suspends play, goes to *Options Screen*
- down button launches a ball, force based on length of press
- back button exits game
- ball movement based on physics engine
- max of 8 balls in play at a time (??)
- uses accelerometer to add nudging to ball movement

## Options Screen
- Uses MenuLayer
  - How to play
  - High Scores
  - Table Select
  - Vibration - on/off
  - Reset balls (with confirmation)
  - Credits
- Back returns to *Game Screen*

# Graphics Assets

## Game Playfield
- Full screen bitmap with black background, round playfield, ball channel

## Ball
- Small round bitmap, black outline, white interior

