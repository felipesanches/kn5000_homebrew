/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * disk.c -- ProTracker's Disk Op. on the KN7000: the modules are the files of the program's folder named *.MOD or MOD.*
 * (as the Amiga names them), sorted by name; the platform reads the folder off the card (PfHost::list). When it cannot,
 * the six of Pinball Fantasies are looked for by name. The list is shown in the right side column (sides.c); the wheel
 * or PAGE UP/DOWN choose, LOAD loads. SAVE writes the module over its own file (asked first); SAVE AS NEW writes
 * PTNEWnn.MOD, the first nn not in the list. A module is saved as pt2-clone's modSave writes it (pt2_module_saver.c). */
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "kn7pt.h"
#include "edit.h"
#include "pt2_structs.h"
#include "pt2_replayer.h"
#include "pt2_module_loader.h"
#include "pt2_module_saver.h"
#include "pt2_textout.h"
#include "pt2_visuals.h"

#define MAX_MODULES 128
static char names[MAX_MODULES][13];
static int moduleCount, moduleAt, listAt;
static bool shown;
static const char *const pinballModules[] = { "INTRO.MOD", "MOD2.MOD", "TABLE1.MOD", "TABLE2.MOD", "TABLE3.MOD", "TABLE4.MOD" };

void restartClock(void);  /* main.c */
void showProgress(const char *what);

int diskModuleAt(void) { return listAt; }
int diskModuleCount(void) { return moduleCount; }
const char *diskModuleName(int i) { return names[i]; }
bool diskShown(void) { return shown; }

static bool isModule(const char *n)
{
	const size_t len = strlen(n);
	return (len > 4 && strcasecmp(n + len - 4, ".MOD") == 0) || (len > 4 && strncasecmp(n, "MOD.", 4) == 0);
}

void diskReadList(void)
{
	uint32_t room;
	PfFile *files = (PfFile *)kn7Scratch(&room);
	const s32 got = host->list(files, room / sizeof (PfFile));
	moduleCount = 0;
	for (s32 i = 0; i < got && moduleCount < MAX_MODULES; i++)
		if (isModule(files[i].name) && files[i].bytes > 0)
		{
			int at = moduleCount++; // sorted as they come in
			while (at > 0 && strcasecmp(names[at - 1], files[i].name) > 0)
			{
				strcpy(names[at], names[at - 1]);
				at--;
			}
			strcpy(names[at], files[i].name);
		}

	if (got < 0)
		for (unsigned i = 0; i < sizeof (pinballModules) / sizeof (pinballModules[0]); i++)
			strcpy(names[moduleCount++], pinballModules[i]);
}

/* An empty module (what ProTracker starts with), when a file cannot be loaded. */
static void emptyModule(void)
{
	module_t *m = createEmptyMod();
	if (m == NULL || !kn7AllocSamples(m) || !kn7AllocPatterns(m, 1))
	{
		host->status("PROTRACKER: OUT OF MEMORY");
		for (;;)
			host->yield(100);
	}

	m->header.initialTempo = 125;
	song = m;
	setupLoadedMod();
	song->loaded = true;
}

bool diskLoad(int index)
{
	if (song != NULL)
	{
		modStop();
		editor.currMode = MODE_IDLE;
		editor.playMode = PLAY_MODE_NORMAL;
		paulaFlush();
		song->loaded = false;
		modFree();
	}

	paulaSetBank(host->dspBase, false);
	restartClock();
	if (index < 0 || index >= moduleCount)
	{
		emptyModule();
		uiFullRedraw();
		return false;
	}

	moduleAt = listAt = index;
	uint32_t room;
	uint8_t *buf = kn7Scratch(&room);
	PfPiece piece = { buf, room };
	const s32 got = host->load(names[index], &piece, 1);
	module_t *m = got > 0 ? modLoadFromBuffer(buf, (uint32_t)got) : NULL;
	if (m == NULL)
	{
		char keep[18];
		strncpy(keep, got > 0 ? ui.statusMessage : "CAN'T FIND FILE !", sizeof (keep) - 1);
		keep[sizeof (keep) - 1] = '\0';
		emptyModule();
		uiFullRedraw();
		displayErrorMsg(keep);
		return false;
	}

	song = m;
	setupLoadedMod();
	song->loaded = true;
	uiFullRedraw();
	showProgress("SENDING SAMPLES..");
	if (bankSend() == 1)
		statusAllRight();
	else
		displayErrorMsg(host->dspVoices ? "DSP NOT READY !" : "NO DSP: SILENT !");
	return true;
}

/* SAVE: over the module's own file; SAVE AS NEW: PTNEWnn.MOD, added to MODS.TXT. The caller has asked. */
void diskSave(bool asNew)
{
	const char *name = names[moduleAt];
	char fresh[13];
	if (asNew || moduleAt >= moduleCount)
	{
		int nn = 1;
		for (; nn <= 99; nn++)
		{
			fmtStr(fmtDec(fmtStr(fresh, "PTNEW", 0), nn, 2, true, false), ".MOD", 0);
			bool taken = false;
			for (int i = 0; i < moduleCount; i++)
				taken = taken || strcmp(names[i], fresh) == 0;
			if (!taken)
				break;
		}
		if (nn > 99 || moduleCount >= MAX_MODULES)
		{
			displayErrorMsg("LIST IS FULL !");
			return;
		}
		name = fresh;
	}

	showProgress("SAVING MODULE...");
	uint32_t room;
	uint8_t *buf = kn7Scratch(&room);
	const uint32_t bytes = modSaveToBuffer(buf, room);
	if (bytes == 0)
	{
		displayErrorMsg("MODULE TOO BIG !");
		return;
	}

	if (host->save(name, buf, bytes) != (s32)bytes)
	{
		displayErrorMsg("FILE I/O ERROR !");
		return;
	}

	if (name == fresh)
	{
		strcpy(names[moduleCount], fresh); // (PTNEWnn sorts among the others: the list is read again next time)
		moduleAt = listAt = moduleCount++;
	}

	song->modified = false;
	displayMsg("MODULE SAVED !");
}

void diskOpen(void)
{
	shown = !shown;
	listAt = moduleAt;
}

/* the Disk Op.'s keys (controls.c hands them over while it is shown) */
void diskKey(int c)
{
	switch (c)
	{
		case -1: listAt = listAt > 0 ? listAt - 1 : 0; break;                            // up
		case +1: listAt = listAt < moduleCount - 1 ? listAt + 1 : moduleCount - 1; break; // down
		case -16: listAt = listAt > 15 ? listAt - 16 : 0; break;
		case +16: listAt = listAt + 16 < moduleCount ? listAt + 16 : moduleCount - 1; break;
		default: break;
	}
}
