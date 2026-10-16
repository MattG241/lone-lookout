/*
 * director.h -- when the music plays (DESIGN.md section 10).
 *
 * Silence and ambience are the default; music is meant to be audible about
 * a quarter to a third of the time. A piece may start on the first arrival
 * at an area each day, at sunrise and golden hour outdoors, after sitting a
 * while, and on a few story beats -- and only if nothing has played for a
 * few minutes. The script's "do music" lines and the credits always play.
 */
#ifndef LL_DIRECTOR_H
#define LL_DIRECTOR_H

void director_init(void);
void director_update(float dt);
void director_story_cue(const char *piece);   /* the script and story beats: always plays */
void director_skip_opening(void);             /* the theme is already playing (from the title) */
float director_music_share(void);             /* fraction of play time with music so far */

#endif
