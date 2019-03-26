/* Alessandro Rubini for CERN, 2014 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
/* for dirname and basename */
#include <libgen.h>
#include <libwr/wrs-msg.h>
#include <libwr/config.h>


#define READ_KCONFIG_MAX_DEPTH 10

struct kc {
	char *name;
	struct kc *next;
};

/* All strings here are strdup'd and then split; you can't free(3) them */
struct cfg_item {
	char *name;
	char *value;
	struct cfg_item *subcfg;
	struct cfg_item *next;
};

static struct cfg_item *libwr_cfg;

static int libwr_subcfg(struct cfg_item *c)
{
	struct cfg_item *subc;
	c->subcfg = NULL;
	char *rest, *s;

	if (!strchr(c->value, '='))
		return 0;

	/* Very lazily, strdup this again, and then modify in place */
	rest = strdup(c->value);
	if (!rest)
		return -1;
	do {
		subc = calloc(1, sizeof(*c));
		if (!subc)
			return -1;
		subc->name = rest;

		/* Trim this and save trailing text */
		s = strchr(rest, ',');
		if (s) {
			rest = s + 1;
			*s = '\0';
		} else {
			rest = NULL;
		}

		/* split between name and value */
		s = strchr(subc->name, '=');
		if (s) {
			subc->value = s + 1;
			*s = '\0';
		}

		/* store and continue, if more is there */
		subc->next = c->subcfg;
		c->subcfg = subc;
	} while (rest);
	return 0;
}

/* Parse a line into name=value and subcfg. Assume dot-config format */
static int libwr_cfg_line(char *line)
{
	struct cfg_item *c;
	char name[512], value[512];
	char * p_value; /* pointer to the beginig of a value */
	char * p_end;   /* pointer to the end of a value */

	if (line[0] == '#')
		return 0;
	if (strlen(line) < 2)
		return 0;

	if (sscanf(line, "CONFIG_%[^=]=%[^\n]", name, value) != 2) {
		errno = EINVAL;
		return -1;
	}

	/* Parse the value in the following way
	 * - skip all leading spaces in the value
	 * - If number or (-), search for "#". Ignore "#" and the rest of
	 *   a line
	 * - If ", search for another " (but make sure it is not escaped),
	 *   then search for "#" and drop the rest. Meanwhile strip quotes */

	p_value = value;
	/* Skip all spaces in value */
	while (*p_value == ' ')
		p_value++;

	if (isdigit(*p_value) || *p_value == '-') { /* parameter is a digit */
		/* Number (including "-"), search for # */
		p_end = strchr(p_value, '#');
		if (p_end) {
			/* Ignore content after # */
			*p_end = '\0';
		}
	} else if (*p_value == 'y' || *p_value == 'n' || *p_value == 'm') {
		/* Recognize single y, n, and m */
		/* Drop everything after recognized char */
		*(p_value + 1) = '\0';
	} else if (*p_value == '"') { /* parameter is a string */
		/* Ignore opening quote */
		p_value++;
		p_end = p_value;
		while (1) {
			p_end = strchr(p_end, '\"');
			if (!p_end) {
				/* End of string */
				break;
			}
			if (*(p_end - 1) == '\\') {
				/* Keep escaped quotes as they where */
				p_end++;
			} else {
				/* Everything including and after closing quote
				 * will be ignored. This also ignores closing
				 * quote */
				*p_end = '\0';
				break;
			}
		}
	} else {
		/* Value has not acceptable form */
		errno = EINVAL;
		return -1;
	}

	c = malloc(sizeof(*c));
	if (!c)
		return -1;
	c->subcfg = NULL;
	c->name = strdup(name);
	c->value = strdup(p_value);
	if (!c->name || !c->value)
		return -1;

	libwr_subcfg(c);

	c->next = libwr_cfg;
	libwr_cfg = c;
	return 0;
}

int libwr_cfg_read_file(char *dotconfig)
{
	char s[512];
	FILE *f;
	int lineno = 0;

	f = fopen(dotconfig, "r");
	if (!f)
		return -1;

	while (fgets(s, sizeof(s), f)) {
		lineno++;
		if (libwr_cfg_line(s) < 0)
			return -lineno;
	}
	fclose(f);
	return 0;
}

int libwr_cfg_dump(FILE *output)
{
	struct cfg_item *c = libwr_cfg;
	struct cfg_item *subc = libwr_cfg;

	while (c) {
		fprintf(output, "name=\"%s\"\n  value=\"%s\"\n",
			c->name, c->value);
		subc = c->subcfg;
		while (subc) {
			fprintf(output, "    name=\"%s\"\n"
				"      value=\"%s\"\n",
				subc->name, subc->value);
			subc = subc->next;
		}
		c = c->next;
	}
	return 0;
}

static int libwr_cfg_read_kconfig(struct kc **all_configs,
				  char *kconfig_dirname,
				  char *kconfig_filename, int depth_level)
{
	FILE *f;
	char s[256], name[256];
	struct kc *kc;
	int ret = 0;
	int len;

	/* Prevent infinite recursion */
	if (depth_level >= READ_KCONFIG_MAX_DEPTH) {
		pr_error("Maximum depth of Kconfig source reached\n");
		return -1;
	}

	/* Check the length of Kconfig path */
	len = strlen(kconfig_dirname) + strlen(kconfig_filename);
	if (len >= 256) {
		pr_error("File path too long %d\n", len);
		return -1;
	}

	snprintf(name, 256, "%s/%s", kconfig_dirname, kconfig_filename);
	pr_debug("Opening Kconfig file %s/%s\n", kconfig_dirname,
		 kconfig_filename);

	/* Read Kconfig and store all config names */
	f = fopen(name, "r");
	if (!f)
		return -1;
	while (fgets(s, sizeof(s), f)) {
		char *ss=s;

		/* Remove leading spaces */
		while ( *ss==' ' || *ss=='\t')
			ss++;

		if (sscanf(ss, "source %s", name) == 1) {
			/* Recursive call for sourced files */
			ret = libwr_cfg_read_kconfig(all_configs,
						     kconfig_dirname, name,
						     depth_level + 1);
			if (ret)
				break;
		}
		if (sscanf(ss, "config %s", name) != 1)
			continue;
		kc = malloc(sizeof(*kc));
		if (!kc) {
			ret = -1;
			break;
		}
		kc->name = strdup(name);
		kc->next = *all_configs;
		*all_configs = kc;
	}
	fclose(f);
	return ret;
}

int libwr_cfg_read_verify_file(char *dotconfig, char *kconfig)
{
	int errors = 0;
	struct cfg_item *c;
	struct kc *all_configs = NULL, *kc, *next;
	int ret = 0;
	char *kconfig_dup1 = NULL;
	char *kconfig_dup2 = NULL;
	char *kconfig_dirname;
	char *kconfig_filename;

	if (libwr_cfg_read_file(dotconfig))
		return -1;

	/* Spearate dirname and basename of Kconfig, use strdup as suggested
	 * in the man page */
	kconfig_dup1 = strdup(kconfig);
	kconfig_dup2 = strdup(kconfig);
	kconfig_dirname = dirname(kconfig_dup1);
	kconfig_filename = basename(kconfig_dup2);

	/* Read Kconfig and store all config names */
	ret = libwr_cfg_read_kconfig(&all_configs, kconfig_dirname,
				     kconfig_filename, 0);
	if (ret) {
		pr_error("Kconfig read error\n");
		return ret;
	}

	/* Verify all configs, complain if missing */
	for (c = libwr_cfg; c; c = c->next) {
		for (kc = all_configs; kc; kc = kc->next)
			if (!strcmp(c->name, kc->name))
				break;
		if (!kc) {
			pr_error("Configuration \"%s\" not found\n",
				c->name);
			errors++;
		}
	}

	/* Free all kconfig allocs */
	for (kc = all_configs; kc; kc = next) {
		next = kc->next;
		free(kc->name);
		free(kc);
	}
	/* free allocated duplicates of kconfig's paths */
	if (kconfig_dup1)
		free(kconfig_dup1);
	if (kconfig_dup2)
		free(kconfig_dup2);

	if (errors) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

char *libwr_cfg_get(char *cfgname)
{
	struct cfg_item *c;

	for (c = libwr_cfg; c; c = c->next)
		if (!strcmp(cfgname, c->name))
			return c->value;
	return NULL;
}

char *libwr_cfg_get2(char *cfgname, char *subname)
{
	struct cfg_item *c, *subc;

	for (c = libwr_cfg; c; c = c->next)
		if (!strcmp(cfgname, c->name)) {
			for (subc = c->subcfg; subc; subc = subc->next)
				if (!strcmp(subname, subc->name))
					return subc->value;
		}
	return NULL;
}

int libwr_cfg_convert2(char *cfgname, char *subname,
			 enum libwr_convert conv, void *ret, ...)
{
	double *dptr = ret;
	int *iptr = ret;
	char real_cfgname[128];
	char *value;
	va_list args;
	char ch;

	va_start(args, ret);
	vsprintf(real_cfgname, cfgname, args);
	va_end(args);

	value = libwr_cfg_get2(real_cfgname, subname);
	if (!value) {
		errno = ENOENT;
		return -1;
	}
	errno = EINVAL;
	switch(conv) {
	case LIBWR_INT:
		if (sscanf(value, "%i%c", iptr, &ch) != 1)
			return -1;
		return 0;
	case LIBWR_STRING:
		strcpy(ret, value);
		return 0;
	case LIBWR_DOUBLE:
		if (sscanf(value, "%lf%c", dptr, &ch) != 1)
			return -1;
		return 0;
	}
	return -1; /* never */
}
