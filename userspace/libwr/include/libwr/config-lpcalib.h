#ifndef __LIBWR_CONFIG2_H__
#define __LIBWR_CONFIG2_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct config_file;

extern int cfg_get_int(struct config_file *cfg, const char *key, int *value);
extern struct config_file *cfg_load(const char *filename, int overwrite);
extern int cfg_set_int(struct config_file *cfg, const char *key, int value);
extern int cfg_set_str(struct config_file *cfg, const char *key, char *value);
extern int cfg_get_str(struct config_file *cfg, const char *key, char *value);
extern int cfg_save(struct config_file *cfg, const char *filename);
extern void cfg_free(struct config_file *cfg);
extern void cfg_close(struct config_file *cfg);

#endif /* __LIBWR_CONFIG2_H__ */

