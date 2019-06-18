/*
 * A replacement for TRACE, which is different things to different people;
 * with no ifdef but only run-time conditionals -- so all code is built
 * and we don't risk rusting unused messages.
 *
 * Unfortunately this is part of libwr, so tools will need to link in
 * libwr. Otherwise we could not host state or hide the varargs things.
 *
 * Some of the things were already done right in <libwr/trace.h>,
 * like choosing a file a runtime etc. Those are preserved here.
 */
#ifndef __WRS_MSG_H__
#define __WRS_MSG_H__

#include <stdio.h>
#include <syslog.h> /* for LOG_ERR, LOG_WARNING etc */

/* We use stderr by default, but we may change file -- or use syslog, later */
extern void wrs_msg_file(FILE *target);
extern void wrs_msg_filename(char *name);

/*
 * The loglevel is global, default to warning, or getenv(); raised/lowered
 * by -v and -q, possibly twice (levels: debug, info, warn, err, nothing).
 * Also, wrs_msg_set_logvelel installs SIGUSR1 and SIGUSR2 handlers;
 * where the former makes more verbose, the letter less verbose
 */
extern int wrs_msg_level; /* user can set it in main() or whatever */

/* Optional: prepare all defaults. Like argv[0] to be prefixed, signals... */
extern void wrs_msg_init(int argc, char **argv, int facility);

#ifdef DEBUG /* We had it, so let's keep this build-time thing */
#  define WRS_MSG_DEFAULT_LEVEL    LOG_DEBUG
#else
#  define WRS_MSG_DEFAULT_LEVEL    LOG_INFO
#endif

#define WRS_MSG_DEFAULT_FACILITY LOG_USER /* User facility by default */

#define WRS_MSG_DETAILS_AT  LOG_DEBUG /* >= for debug use __LINE__ */

/* This is the external function for it all */
extern void __wrs_msg(int level, const char *func, int line,
		      const char *fmt, ...)
	__attribute__((format(printf,4,5)));

/* The suggested use */
#define wrs_msg(lev, ...) __wrs_msg(lev, __func__, __LINE__, __VA_ARGS__)

/* And shortands for people using autocompletion -- but not all levels */
#define pr_error(...)	wrs_msg(LOG_ERR, __VA_ARGS__)
#define pr_err(...)	wrs_msg(LOG_ERR, __VA_ARGS__)
#define pr_warning(...)	wrs_msg(LOG_WARNING, __VA_ARGS__)
#define pr_warn(...)	wrs_msg(LOG_WARNING, __VA_ARGS__)
#define pr_info(...)	wrs_msg(LOG_INFO, __VA_ARGS__)
#define pr_debug(...)	wrs_msg(LOG_DEBUG, __VA_ARGS__)

#define assert_init(proc) { \
	int ret = proc; \
	if (ret < 0) { \
		pr_error("Error in function "#proc" ret = %d\n", ret); \
		return ret; \
	} \
}

#endif /* __WRS_MSG_H__ */
