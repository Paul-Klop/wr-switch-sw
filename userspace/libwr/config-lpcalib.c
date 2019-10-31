/*
 * White Rabbit parser for Low Phase Drift Calibration configuration
 * Copyright (C) 2018, CERN.
 *
 * Author:      Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Description: re-written parser for configuration file. It allows
 * to simultaneously read/write few files. 
 *
 * NOTE: consider merging this with dot-config.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libwr/config-lpcalib.h>

struct key_value
{
	char *comment;
	char *key;
	char *value;
	struct key_value *next;
};

struct config_file
{
	struct key_value *head;
};

static struct key_value *cfg_find_key(struct config_file *cfg, const char *key, int create)
{
	struct key_value *kv = cfg->head, *kv_prev = cfg->head;

	while (kv)
	{
		if (kv->key && !strcasecmp(key, kv->key))
		{
			return kv;
		}
		kv_prev = kv;
		kv = kv->next;
	}

	if (!create)
		return NULL;

	if (!cfg->head)
	{
		cfg->head = malloc(sizeof(struct key_value));
		kv = cfg->head;
	}
	else
	{
		kv_prev->next = malloc(sizeof(struct key_value));
		kv = kv_prev->next;
	}

	kv->key = strdup(key);
	kv->value = NULL;
	kv->comment = NULL;
	kv->next = NULL;

	return kv;
}

int cfg_get_int(struct config_file *cfg, const char *key, int *value)
{
	struct key_value *kv = cfg_find_key(cfg, key, 0);

	if (!kv)
		return 0;

	*value = atoi(kv->value);
	return 1;
}

struct config_file *cfg_load(const char *filename, int overwrite)
{
	FILE *f = fopen(filename, "rb");
	if (!f)
	{

		if (overwrite)
		{
			struct config_file *cfg = malloc(sizeof(struct config_file));

			f = fopen(filename, "wb");

			cfg->head = NULL;

			return cfg;
		}
		return NULL;
	}

	struct config_file *cfg = malloc(sizeof(struct config_file));

	cfg->head = NULL;

	while (!feof(f))
	{
		char str[1024];
		int rv = (int)fgets(str, 1024, f);
		int i, pos = -1;

		if (rv == 0)
			break;

		for (i = 0; i < strlen(str); i++)
		{
			if (str[i] == '=' && pos < 0)
				pos = i;
			if (str[i] == '\n' || str[i] == '\r')
				str[i] = 0;
		}

		struct key_value *kv = malloc(sizeof(struct key_value));
		if (pos < 0 || strlen(str) == 0 || str[0] == '#')
		{
			kv->comment = strdup(str);
			kv->key = NULL;
			kv->value = NULL;
		}
		else
		{
			kv->comment = NULL;
			kv->key = strndup(str, pos);
			kv->value = strdup(str + pos + 1);
			kv->next = NULL;
		}

		if (!cfg->head)
		{
			cfg->head = kv;
		}
		else
		{
			struct key_value *last = cfg->head;
			while (last->next)
				last = last->next;
			last->next = kv;
		}
	}

	return cfg;
}

int cfg_set_int(struct config_file *cfg, const char *key, int value)
{
	struct key_value *kv = cfg_find_key(cfg, key, 1);

	if (!kv)
		return 0;

	char buf[1024];
	snprintf(buf, 1024, "%d", value);

	kv->value = strdup(buf);
	return 1;
}

int cfg_save(struct config_file *cfg, const char *filename)
{
	FILE *f = fopen(filename, "wb");

	if (!f)
	{
		printf("Can't open '%s' for writing.\n", filename);
		return -1;
	}

	struct key_value *kv = cfg->head;

	while (kv)
	{
		if (kv->comment)
			fprintf(f, "%s\n", kv->comment);
		else
			fprintf(f, "%s=%s\n", kv->key, kv->value);

		kv = kv->next;
	}

	fclose(f);
	return 0;
}

void cfg_close(struct config_file *cfg)
{
	struct key_value *kv = cfg->head;

	while (kv)
	{
		if (kv->comment)
			free(kv->comment);
		if (kv->key)
			free(kv->key);
		if (kv->value)
			free(kv->value);
		struct key_value *ptr = kv;
		kv = kv->next;
		free(ptr);
	}
}
