#ifndef __LIBWR_CONFIG2_H__
#define __LIBWR_CONFIG2_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct config_file;

int cfg_get_int(struct config_file *cfg, const char *key, int *value);
struct config_file *cfg_load(const char *filename, int overwrite);
int cfg_set_int(struct config_file *cfg, const char *key, int value);
int cfg_save(struct config_file *cfg, const char *filename);
void cfg_free(struct config_file *cfg);

#endif /* __LIBWR_CONFIG2_H__ */

