/*
 * save.h -- the save file, sd:/apps/lonelookout/save.dat (DESIGN.md section 11).
 *
 * One small file: a header (magic, version, length, CRC32 of the body), the
 * settings, then -- once a season is under way -- the game: the day and
 * clock, where Theo stands, the notes and sounds found, the story and radio
 * script state (by name), the errand and the radio log.
 *
 * It is written on sleep (each morning) and on Save & quit, and whenever the
 * options change. Without an SD card the game plays the same; nothing keeps.
 */
#ifndef LL_SAVE_H
#define LL_SAVE_H

#include <stdbool.h>

#define SAVE_NAME "save.dat"

typedef struct {
	bool card;            /* an SD card is mounted */
	bool has_game;        /* a season to continue */
	int day;
	float hour;
} save_summary;

void save_init(void);                 /* read the file at boot; apply the settings */
save_summary save_status(void);
bool save_game(void);                 /* settings and the season as it stands */
bool save_settings(void);             /* settings only; any saved season is kept */
bool save_continue(void);             /* load the saved season into the game */
void save_forget_game(void);          /* "New season": the old one is dropped on the next save */

#endif
