/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * vmu_save.c: the board's non-volatile memory on a VMU. The SDL port keeps the same two things in
 * orunners.eeprom and orunners.bookkeeping (ports/sdl2/main.c): the 93C46 EEPROM's 64 words, and the
 * sound board's RAM from 0x1F00 (0xFA bytes: the checksum, course records, bookkeeping and credits).
 * Both go in one VMU file, ORUNNERS, with a header and an original icon (vmu_icon.h). A change is
 * checked for every 600 frames, as the SDL port does; the write itself runs on a thread of its own, so
 * the VMU's slow block writes never hold up a frame.
 */
#include "vmu_save.h"
#include "vmu_icon.h"
#include <kos.h>
#include <dc/vmu_pkg.h>
#include <dc/maple.h>
#include <s32/machine.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#define BACKUP_RAM_START 0x1f00
#define BACKUP_RAM_SIZE  0xfa
#define FILE_NAME        "ORUNNERS"

typedef struct
{
	char magic[4];                         /* "ORDC" */
	uint32_t version;                      /* 1 */
	uint16_t eeprom[S32_EEPROM_WORDS];
	uint8_t backup[BACKUP_RAM_SIZE];
	uint8_t options[DC_OPTIONS];            /* the console menus' own (version 2; vmu_save.h) */
} save_data;
#define V1_SIZE offsetof(save_data, options)

uint8_t dc_options[DC_OPTIONS];
bool dc_options_loaded;

volatile uint32_t dc_vmu_state;
static save_data saved;                     /* as the VMU has it (or nothing yet) */
static save_data pending;                   /* the copy the saver thread writes */
static semaphore_t work;
static kthread_t *saver;
static volatile int busy;

static int vmu_path(char *path, size_t size)
{
	maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_MEMCARD);
	if (!dev) return -1;
	snprintf(path, size, "/vmu/%c%d/" FILE_NAME, 'a' + dev->port, dev->unit);
	return 0;
}

static void current(save_data *d)
{
	memset(d, 0, sizeof(*d));
	memcpy(d->magic, "ORDC", 4);
	d->version = 2;
	memcpy(d->options, dc_options, DC_OPTIONS);
	memcpy(d->eeprom, s32_board.eeprom.data, sizeof(d->eeprom));
	memcpy(d->backup, &s32_board.shared_ram[BACKUP_RAM_START], BACKUP_RAM_SIZE);
}

static int write_save(const save_data *d)
{
	char path[64];
	if (vmu_path(path, sizeof(path)) != 0) return -1;
	vmu_pkg_t pkg;
	memset(&pkg, 0, sizeof(pkg));
	/* (the file header's fields are 16, 32 and 16 bytes, and vmu_pkg_build copies whole strings: each
	   must fit, or it runs into the next field and the file no longer parses) */
	strcpy(pkg.desc_short, "OutRunners");
	strcpy(pkg.desc_long, "OutRunners records & settings");
	strcpy(pkg.app_id, "OUTRUNNERS_DC");
	pkg.icon_cnt = 1;
	pkg.icon_anim_speed = 0;
	pkg.eyecatch_type = VMUPKG_EC_NONE;
	memcpy(pkg.icon_pal, vmu_icon_palette, sizeof(pkg.icon_pal));
	pkg.icon_data = (uint8_t *)vmu_icon_pixels;    /* (KOS wants it non-const; only read) */
	pkg.data_len = sizeof(*d);
	pkg.data = (const uint8_t *)d;
	uint8_t *out = NULL;
	int out_size = 0;
	if (vmu_pkg_build(&pkg, &out, &out_size) < 0 || !out) return -1;
	int ok = -1;
	file_t f = fs_open(path, O_WRONLY | O_TRUNC);
	if (f != FILEHND_INVALID)
	{
		if (fs_write(f, out, (size_t)out_size) == out_size) ok = 0;
		if (fs_close(f) != 0) ok = -1;
	}
	free(out);
	return ok;
}

static void *saver_main(void *arg)
{
	(void)arg;
	for (;;)
	{
		sem_wait(&work);
		if (write_save(&pending) == 0)
		{
			saved = pending;
			dc_vmu_state += 1u << 8;
		}
		else
			dc_vmu_state += 1u << 24;
		busy = 0;
	}
	return NULL;
}

int dc_vmu_load(void)
{
	char path[64];
	int result = -1;
	sem_init(&work, 0);
	kthread_attr_t attr = { 0 };
	attr.stack_size = 16 * 1024;
	attr.prio = PRIO_DEFAULT;
	attr.label = "vmu-save";
	saver = thd_create_ex(&attr, saver_main, NULL);
	if (vmu_path(path, sizeof(path)) == 0)
	{
		result = 0;
		file_t f = fs_open(path, O_RDONLY);
		uint32_t why = 0x10;    /* (for the capture, bits 4-7: open, read, header, contents failed) */
		if (f != FILEHND_INVALID)
		{
			why = 0x20;
			const ssize_t size = (ssize_t)fs_total(f);
			uint8_t *buf = size > 0 && size < 65536 ? malloc((size_t)size) : NULL;
			if (buf && fs_read(f, buf, (size_t)size) == size)
			{
				/* this KOS's VMU file system reads a game file back without its header and icon (seen in
				   Flycast: the data alone, from "ORDC"); a whole package is parsed as well */
				save_data d;
				const uint8_t *payload = NULL;
				size_t payload_size = (size_t)size;
				why = 0x40;
				if ((size_t)size >= V1_SIZE && memcmp(buf, "ORDC", 4) == 0)
					payload = buf;
				else
				{
					vmu_pkg_t pkg;
					if (vmu_pkg_parse(buf, (size_t)size, &pkg) == 0 && pkg.data_len >= (int)V1_SIZE)
					{
						payload = pkg.data;
						payload_size = (size_t)pkg.data_len;
					}
				}
				if (payload)
				{
					why = 0x80;
					memset(&d, 0, sizeof(d));
					memcpy(&d, payload, payload_size < sizeof(d) ? payload_size : sizeof(d));
					if (memcmp(d.magic, "ORDC", 4) == 0 && (d.version == 1 || (d.version == 2 && payload_size >= sizeof(d))))
					{
						if (d.version == 2)
						{
							memcpy(dc_options, d.options, DC_OPTIONS);
							dc_options_loaded = true;
						}
						why = 0;
						memcpy(s32_board.eeprom.data, d.eeprom, sizeof(d.eeprom));
						memcpy(&s32_board.shared_ram[BACKUP_RAM_START], d.backup, BACKUP_RAM_SIZE);
						saved = d;
						result = 1;
					}
				}
			}
			free(buf);
			fs_close(f);
		}
		dc_vmu_state |= why;
	}
	if (result != 1) current(&saved);    /* (nothing to compare against yet: the first change is saved) */
	dc_vmu_state = (dc_vmu_state & 0xf0u) | (uint32_t)(result + 2);
	return result;
}

/* The course records (0x32-0x73 of the kept RAM: 701F32 to the credits at 701F74) and the EEPROM's
   settings go to the VMU within ten seconds of changing; the rest (the bookkeeping, which counts play
   time and changes all the time) only with them, or every five minutes, to spare the VMU's flash */
#define RECORDS_START 0x32
#define RECORDS_END   0x74
static uint32_t last_save_frame;

void dc_vmu_frame(uint32_t frame)
{
	if (frame % 600 != 0 || busy || !saver)
		return;
	current(&pending);
	if (memcmp(&pending, &saved, sizeof(pending)) == 0)
		return;
	const bool important = memcmp(pending.eeprom, saved.eeprom, sizeof(saved.eeprom)) != 0 ||
		memcmp(pending.options, saved.options, DC_OPTIONS) != 0 ||
		memcmp(&pending.backup[RECORDS_START], &saved.backup[RECORDS_START], RECORDS_END - RECORDS_START) != 0;
	if (!important && frame - last_save_frame < 18000)
		return;
	last_save_frame = frame;
	busy = 1;
	sem_signal(&work);
}
