/*
 * notebook.h -- Theo's notebook: Augie's map, the notes found so far, the
 * radio log, a page kept for sounds, and today's duties.
 */
#ifndef LL_NOTEBOOK_H
#define LL_NOTEBOOK_H

enum { NB_TODAY, NB_MAP, NB_NOTES, NB_RADIO, NB_SOUNDS, NB_PLACES, NB_MOMENTS, NB_PAGES };

void notebook_update(float dt);
void notebook_draw(void);

#endif
