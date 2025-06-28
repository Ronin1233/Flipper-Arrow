#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include "music.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define ARROW_SIZE 10
#define HIT_ZONE_Y (SCREEN_HEIGHT - 15)
#define HIT_ZONE_RANGE 10

#define HIT_ANIM_DURATION 200 // мс

#define RECORD_FILE_PREFIX "/ext/flipper_arrow_record_"

typedef enum {
    STATE_MENU,
    STATE_PLAYING,
    STATE_GAME_OVER
} GameState;

typedef struct {
    Direction dir;
    int16_t y;
    bool active;

    // Анимация попадания
    bool hit_animation_active;
    uint32_t hit_animation_start;
} Arrow;

typedef struct {
    GameState state;
    Difficulty difficulty;

    const NoteEvent* rhythm;
    size_t rhythm_length;
    size_t rhythm_index;

    Arrow current_arrow;

    int32_t score;
    int32_t record;

    uint32_t last_note_time;
    uint32_t note_interval;
    int16_t arrow_speed;

    ViewPort* view_port;
    Gui* gui;

    Storage* storage;
} FlipperArrow;

static FlipperArrow* game = NULL;

// Вспомогательная функция для получения имени файла рекорда по сложности
static void get_record_filename(Difficulty diff, char* out, size_t max_len) {
    const char* suffix = "";
    switch(diff) {
        case DIFFICULTY_EASY: suffix = "easy.txt"; break;
        case DIFFICULTY_MEDIUM: suffix = "medium.txt"; break;
        case DIFFICULTY_HARD: suffix = "hard.txt"; break;
    }
    snprintf(out, max_len, "%s%s", RECORD_FILE_PREFIX, suffix);
}

// Загрузка рекорда из файла
static int32_t load_record(Storage* storage, Difficulty diff) {
    char filename[64];
    get_record_filename(diff, filename, sizeof(filename));
    uint8_t buffer[16] = {0};
    uint32_t bytes_read = 0;
    if(storage_file_read(storage, filename, buffer, sizeof(buffer), &bytes_read)) {
        if(bytes_read > 0) {
            return atoi((char*)buffer);
        }
    }
    return 0;
}

// Сохранение рекорда в файл
static void save_record(Storage* storage, Difficulty diff, int32_t record) {
    char filename[64];
    get_record_filename(diff, filename, sizeof(filename));
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", record);
    storage_file_write(storage, filename, (uint8_t*)buf, strlen(buf));
}

static void draw_arrow(Canvas* canvas, Direction dir, int16_t y, bool hit_anim_active, uint32_t anim_start) {
    int x = SCREEN_WIDTH / 2;
    int y_pos = y;

    // Позиция стрелки по направлению
    switch(dir) {
        case DIR_UP: x = SCREEN_WIDTH / 2; break;
        case DIR_DOWN: x = SCREEN_WIDTH / 2; break;
        case DIR_LEFT: x = SCREEN_WIDTH / 4; break;
        case DIR_RIGHT: x = 3 * SCREEN_WIDTH / 4; break;
        default: return;
    }

    int size = ARROW_SIZE;

    if(hit_anim_active) {
        // Считаем сколько прошло времени от начала анимации
        uint32_t now = furi_get_tick();
        uint32_t elapsed = now - anim_start;
        if(elapsed < HIT_ANIM_DURATION) {
            // Уменьшаем размер (на 70%)
            float factor = 1.0f - 0.7f * ((float)elapsed / HIT_ANIM_DURATION);
            size = (int)(ARROW_SIZE * factor);
        } else {
            // Анимация закончилась
            size = 0;
        }
    }

    if(size <= 0) return;

    int half = size / 2;
    switch(dir) {
        case DIR_UP:
            canvas_draw_triangle(canvas,
                x, y_pos - half,
                x - half, y_pos + half,
                x + half, y_pos + half,
                true);
            break;
        case DIR_DOWN:
            canvas_draw_triangle(canvas,
                x, y_pos + half,
                x - half, y_pos - half,
                x + half, y_pos - half,
                true);
            break;
        case DIR_LEFT:
            canvas_draw_triangle(canvas,
                x - half, y_pos,
                x + half, y_pos - half,
                x + half, y_pos + half,
                true);
            break;
        case DIR_RIGHT:
            canvas_draw_triangle(canvas,
                x + half, y_pos,
                x - half, y_pos - half,
                x - half, y_pos + half,
                true);
            break;
        default:
            break;
    }
}

static void draw_menu(Canvas* canvas) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH/2, 10, AlignCenter, "Flipper Arrow");

    const char* options[] = {"Easy", "Medium", "Hard", "Exit"};
    for(int i = 0; i < 4; i++) {
        if(i == (int)game->difficulty) {
            canvas_draw_str_aligned(canvas, SCREEN_WIDTH/2 - 20, 25 + i*12, AlignCenter, ">");
        }
        canvas_draw_str_aligned(canvas, SCREEN_WIDTH/2 + 10, 25 + i*12, AlignCenter, options[i]);
    }
}

static void draw_game(Canvas* canvas) {
    canvas_clear(canvas);

    // Рисуем стрелку
    if(game->current_arrow.active) {
        draw_arrow(canvas,
            game->current_arrow.dir,
            game->current_arrow.y,
            game->current_arrow.hit_animation_active,
            game->current_arrow.hit_animation_start);
    }

    // Зона попадания
    canvas_draw_line(canvas, 0, HIT_ZONE_Y, SCREEN_WIDTH, HIT_ZONE_Y);
    canvas_draw_line(canvas, 0, HIT_ZONE_Y - HIT_ZONE_RANGE, SCREEN_WIDTH, HIT_ZONE_Y - HIT_ZONE_RANGE);

    // Счёт и рекорд
    char score_str[32];
    snprintf(score_str, sizeof(score_str), "Score: %d  Record: %d", game->score, game->record);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH/2, SCREEN_HEIGHT - 5, AlignCenter, score_str);
}

static void draw_game_over(Canvas* canvas) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 10, AlignCenter, "Game Over");
    char score_str[32];
    snprintf(score_str, sizeof(score_str), "Score: %d  Record: %d", game->score, game->record);
    canvas_draw_str_aligned(canvas, SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 10, AlignCenter, score_str);

    canvas_draw_str_aligned(canvas, SCREEN_WIDTH/2, SCREEN_HEIGHT - 10, AlignCenter, "Press BACK");
}

static void process_menu_input(InputEvent* input) {
    if(input->type == InputTypeShort) {
        if(input->key == InputKeyUp) {
            if(game->difficulty > 0) game->difficulty--;
            else game->difficulty = 3; // wrap to Exit
        } else if(input->key == InputKeyDown) {
            if(game->difficulty < 3) game->difficulty++;
            else game->difficulty = 0;
        } else if(input->key == InputKeyOk) {
            if(game->difficulty == 3) {
                // Exit
                furi_thread_exit(NULL);
            } else {
                // Запускаем игру
                game->rhythm = get_rhythm(game->difficulty, &game->rhythm_length);
                game->rhythm_index = 0;
                game->score = 0;
                game->current_arrow.active = false;
                game->current_arrow.hit_animation_active = false;
                game->state = STATE_PLAYING;
                game->record = load_record(game->storage, game->difficulty);

                // Интервал и скорость по сложности
                switch(game->difficulty) {
                    case DIFFICULTY_EASY:
                        game->note_interval = 1000;
                        game->arrow_speed = 2;
                        break;
                    case DIFFICULTY_MEDIUM:
                        game->note_interval = 700;
                        game->arrow_speed = 3;
                        break;
                    case DIFFICULTY_HARD:
                        game->note_interval = 500;
                        game->arrow_speed = 4;
                        break;
                }
                game->last_note_time = furi_get_tick();
            }
        }
    }
}

static void process_game_input(InputEvent* input) {
    if(input->type != InputTypeShort) return;

    if(!game->current_arrow.active) return;

    Direction expected = game->current_arrow.dir;
    Direction pressed = DIR_NONE;

    switch(input->key) {
        case InputKeyUp: pressed = DIR_UP; break;
        case InputKeyDown: pressed = DIR_DOWN; break;
        case InputKeyLeft: pressed = DIR_LEFT; break;
        case InputKeyRight: pressed = DIR_RIGHT; break;
        default: return;
    }

    if(game->current_arrow.hit_animation_active) return; // не реагируем пока анимация идёт

    // Проверка попадания
    if(pressed == expected &&
       game->current_arrow.y >= HIT_ZONE_Y - HIT_ZONE_RANGE &&
       game->current_arrow.y <= HIT_ZONE_Y + HIT_ZONE_RANGE) {
        // Попадание — запускаем анимацию уменьшения
        game->score++;
        game->current_arrow.hit_animation_active = true;
        game->current_arrow.hit_animation_start = furi_get_tick();

        // Звук попадания (короткий щелчок)
        furi_hal_sound_play(furi_hal_sound_get_instance(), 1000, 100);

        // Ускоряем игру каждые 5 очков (мин интервал 200)
        if(game->score % 5 == 0 && game->note_interval > 200) {
            game->note_interval -= 50;
            game->arrow_speed += 1;
        }
    } else {
        // Ошибка — конец игры
        game->state = STATE_GAME_OVER;
        notification_message("Missed! Game Over");

        // Проверяем рекорд
        if(game->score > game->record) {
            game->record = game->score;
            save_record(game->storage, game->difficulty, game->record);
            notification_message("New Record!");
        }
    }
}

static void flipper_arrow_draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    switch(game->state) {
        case STATE_MENU:
            draw_menu(canvas);
            break;
        case STATE_PLAYING:
            draw_game(canvas);
            break;
        case STATE_GAME_OVER:
            draw_game_over(canvas);
            break;
    }
}

int32_t flipper_arrow_app(void* p) {
    UNUSED(p);

    FlipperArrow local_game = {0};
    game = &local_game;

    game->difficulty = DIFFICULTY_EASY;
    game->state = STATE_MENU;

    ViewPort* view_port = view_port_alloc();
    game->view_port = view_port;

    Gui* gui = furi_record_open("gui");
    game->gui = gui;

    Storage* storage = furi_record_open("storage");
    game->storage = storage;

    view_port_draw_callback_set(view_port, flipper_arrow_draw_callback, NULL);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    while(true) {
        InputEvent input;
        if(furi_hal_delay_ms(10), furi_input_get(&input)) {
            switch(game->state) {
                case STATE_MENU:
                    process_menu_input(&input);
                    break;
                case STATE_PLAYING:
                    process_game_input(&input);
                    break;
                case STATE_GAME_OVER:
                    if(input.type == InputTypeShort && input.key == InputKeyBack) {
                        game->state = STATE_MENU;
                    }
                    break;
            }
        }

        if(game->state == STATE_PLAYING) {
            uint32_t now = furi_get_tick();

            // Проверяем ноту
            if(now - game->last_note_time >= game->note_interval) {
                if(game->rhythm_index < game->rhythm_length) {
                    NoteEvent note = game->rhythm[game->rhythm_index];
                    game->rhythm_index++;
                    game->last_note_time = now;

                    if(note.frequency > 0) {
                        furi_hal_sound_play(furi_hal_sound_get_instance(), note.frequency, note.duration);
                    }

                    if(note.dir != DIR_NONE && !game->current_arrow.active) {
                        game->current_arrow.dir = note.dir;
                        game->current_arrow.y = 0;
                        game->current_arrow.active = true;
                        game->current_arrow.hit_animation_active = false;
                    }
                } else {
                    // Конец музыки — игра закончена
                    game->state = STATE_GAME_OVER;

                    // Проверка рекорда
                    if(game->score > game->record) {
                        game->record = game->score;
                        save_record(game->storage, game->difficulty, game->record);
                        notification_message("New Record!");
                    }
                }
            }

            // Двигаем стрелку вниз, если активна и анимация не идёт
            if(game->current_arrow.active && !game->current_arrow.hit_animation_active) {
                game->current_arrow.y += game->arrow_speed;

                if(game->current_arrow.y > HIT_ZONE_Y + HIT_ZONE_RANGE) {
                    // Промах
                    game->state = STATE_GAME_OVER;
                    notification_message("Missed! Game Over");

                    if(game->score > game->record) {
                        game->record = game->score;
                        save_record(game->storage, game->difficulty, game->record);
                        notification_message("New Record!");
                    }
                }
            } else if(game->current_arrow.hit_animation_active) {
                // Проверяем окончание анимации уменьшения
                uint32_t elapsed = furi_get_tick() - game->current_arrow.hit_animation_start;
                if(elapsed >= HIT_ANIM_DURATION) {
                    game->current_arrow.active = false;
                    game->current_arrow.hit_animation_active = false;
                }
            }
        }

        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);

    return 0;
}