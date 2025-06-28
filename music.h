#pragma once

#include <stdint.h>

typedef enum {
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_NONE,
} Direction;

typedef struct {
    uint16_t frequency;
    uint16_t duration;
    Direction dir;
} NoteEvent;

typedef enum {
    DIFFICULTY_EASY,
    DIFFICULTY_MEDIUM,
    DIFFICULTY_HARD,
} Difficulty;

const NoteEvent* get_rhythm(Difficulty diff, size_t* length);