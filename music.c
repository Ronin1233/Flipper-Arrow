#include "music.h"

const NoteEvent easy_rhythm[] = {
    {440, 500, DIR_LEFT},
    {0,   300, DIR_NONE},
    {440, 500, DIR_RIGHT},
    {0,   300, DIR_NONE},
    {660, 500, DIR_UP},
    {0,   300, DIR_NONE},
    {440, 500, DIR_DOWN},
    {0,   300, DIR_NONE},
};

const NoteEvent medium_rhythm[] = {
    {660, 400, DIR_RIGHT},
    {0,   200, DIR_NONE},
    {660, 400, DIR_LEFT},
    {880, 400, DIR_UP},
    {660, 400, DIR_DOWN},
};

const NoteEvent hard_rhythm[] = {
    {880, 300, DIR_DOWN},
    {660, 300, DIR_LEFT},
    {880, 300, DIR_RIGHT},
    {660, 300, DIR_UP},
    {1040, 300, DIR_LEFT},
    {880, 300, DIR_RIGHT},
};

const NoteEvent* get_rhythm(Difficulty diff, size_t* length) {
    if(diff == DIFFICULTY_EASY) {
        *length = sizeof(easy_rhythm)/sizeof(NoteEvent);
        return easy_rhythm;
    } else if(diff == DIFFICULTY_MEDIUM) {
        *length = sizeof(medium_rhythm)/sizeof(NoteEvent);
        return medium_rhythm;
    } else {
        *length = sizeof(hard_rhythm)/sizeof(NoteEvent);
        return hard_rhythm;
    }
}