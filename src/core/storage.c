/*
 * storage.c -- see storage.h.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fat.h>
#include "core/storage.h"

static bool s_ok;

void storage_init(void)
{
	if (!fatInitDefault()) return;
	/* The Homebrew Channel layout keeps us in sd:/apps/lonelookout/; make sure
	 * it exists (it won't when the .dol is run straight from Dolphin). */
	mkdir("sd:/apps", 0777);
	mkdir(STORAGE_DIR, 0777);
	struct stat st;
	s_ok = stat(STORAGE_DIR, &st) == 0 && S_ISDIR(st.st_mode);
}

bool storage_ok(void) { return s_ok; }

int storage_read(const char *name, void *buf, u32 cap)
{
	if (!s_ok) return -1;
	char path[128];
	snprintf(path, sizeof(path), STORAGE_DIR "/%s", name);
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	int n = (int)fread(buf, 1, cap, f);
	fclose(f);
	return n;
}

bool storage_write(const char *name, const void *buf, u32 n)
{
	if (!s_ok) return false;
	char path[128], tmp[128];
	snprintf(path, sizeof(path), STORAGE_DIR "/%s", name);
	snprintf(tmp, sizeof(tmp), STORAGE_DIR "/%s.tmp", name);
	FILE *f = fopen(tmp, "wb");
	if (!f) return false;
	bool ok = fwrite(buf, 1, n, f) == n;
	ok = (fclose(f) == 0) && ok;
	if (!ok) {
		remove(tmp);
		return false;
	}
	remove(path);
	return rename(tmp, path) == 0;
}
