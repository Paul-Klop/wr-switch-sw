#ifndef __WRSW_HAL_H
#define __WRSW_HAL_H

#include <inttypes.h>

typedef void (*hal_cleanup_callback_t)(void);

int hal_check_running(void);

int hal_parse_config(void);
void hal_config_set_config_file(const char *str);
int hal_config_extra_cmdline(const char *str);
int hal_config_get_int(const char *name, int *value);
int hal_config_get_double(const char *name, double *value);
int hal_config_get_string(const char *name, char *value, int max_len);
int hal_config_iterate(const char *section, int index,
		       char *subsection, int max_len);

int hal_port_init_shmem(char *logfilename);
int hal_port_init_wripc(char *logfilename);
void hal_port_update_all(void);
struct hexp_port_state;
struct hal_port_state;

int hal_init_wripc(struct hal_port_state *hal_ports, char *logfilename);
int hal_update_wripc(int ms_timeout);

int hal_add_cleanup_callback(hal_cleanup_callback_t cb);

int hal_port_start_lock(const char *port_name, int priority);
int hal_port_check_lock(const char *port_name);
int hal_port_reset(const char *port_name);
int hal_port_enable_tracking(const char *port_name);

int hal_init_timing_mode(void);
int hal_init_timing(char *filename);
int hal_get_timing_mode(void);
int hal_port_pshifter_busy(void);

#endif
