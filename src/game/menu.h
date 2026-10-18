/*
 * menu.h -- the title screen, the pause menu and the Options screen.
 *
 * The title shows Augie's bench at golden hour with the theme playing:
 * Continue (the saved day), New season, Options, and back to the Homebrew
 * Channel. In play, HOME or + pauses (time stops): Resume, Options, Save and
 * return to the title, Quit. Options change as you move through them and are
 * saved when you leave the screen.
 */
#ifndef LL_MENU_H
#define LL_MENU_H

#include <stdbool.h>

typedef enum { APP_TITLE, APP_PLAY } app_state;
extern app_state APP;

void title_enter(void);               /* the title screen, over the bench at golden hour */
void title_update(float dt);          /* the scene behind the title */
void menu_pause(void);                /* open the pause menu (in play) */
void menu_close(void);
bool menu_active(void);               /* a menu has the controls (time stops in play) */
void menu_update(float dt);
void menu_draw(void);
bool menu_quit_requested(void);       /* back to the Homebrew Channel */
/* For the scripted menu test: which screen is up. */
enum { MENU_NONE, MENU_TITLE, MENU_PAUSE, MENU_OPTIONS, MENU_CONFIRM_NEW };
int menu_screen(void);
void menu_debug_screen(int screen, int selected);   /* scripted screenshot tour */

#endif
