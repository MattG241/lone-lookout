/*
 * storage.h -- the SD card: mounted once at boot, then small whole-file
 * reads and safe writes (to a temporary file, then renamed over the old one,
 * so a power cut mid-save never leaves a half-written save behind).
 */
#ifndef LL_STORAGE_H
#define LL_STORAGE_H

#include <gctypes.h>
#include <stdbool.h>

#define STORAGE_DIR "sd:/apps/lonelookout"

void storage_init(void);                 /* mount the card; harmless without one */
bool storage_ok(void);                   /* a card is mounted and writable */
/* Read a whole file into buf; returns bytes read, or -1 if missing. */
int storage_read(const char *name, void *buf, u32 cap);
bool storage_write(const char *name, const void *buf, u32 n);

#endif
