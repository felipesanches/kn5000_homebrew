/* (c) 2026 Felipe Correa da Silva Sanches <juca@members.fsf.org>
 * Licensed under the terms of the GNU General Public License v3 or later
 *
 * edit.h -- edit.c (pattern editing), song.c (the song, the sample, the options), disk.c (load, save). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* edit.c */
extern bool editChord, editVelocity;
bool editing(void);
void editCursorRight(void);
void editCursorLeft(void);
void editCursorNextChannel(void);
void editCursorChannel(int ch);
void editRowMove(int d);
void editRowSet(int row);
void editNotes(const uint8_t *notes, const uint8_t *vels, int n);
void editHex(int key);
void editDelete(bool all);
void editInsertRow(bool all);
void editDeleteRow(bool all);
void editTranspose(int scope, int semitones);
void editBuffer(int op, bool wholePattern);
void editMacro(int slot, bool store);
void editUndo(void);
void editRest(void);
void editStepPlay(void);
void editModWheel(int value);
void editNudge(int d);

/* song.c */
enum { F_POS, F_PATT, F_LEN, F_BPM, F_SAMPLE, F_VOLUME, F_FINETUNE, F_SKIP, F_REPEAT, F_REPLEN, F_METRO_SPEED,
       F_METRO_CH, F_SPEED, F_EDIT_PATT, F_COUNT };
void songField(int field, int d);       /* the field a step up (d > 0) or down */
int songFieldValue(int field);
const char *songFieldName(int field);
void songInsertPosition(void);
void songDeletePosition(void);
void songClear(void);
void songTapTempo(void);
void songSolo(int ch);
void songMute(int ch, bool mute);

/* disk.c */
void diskOpen(void);
bool diskShown(void);
void diskKey(int c);                    /* a control while the Disk Op. is shown */
void diskSave(bool asNew);
bool diskLoad(int index);
int diskModuleAt(void);
int diskModuleCount(void);
const char *diskModuleName(int i);
void diskReadList(void);
