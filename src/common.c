/**
 * collectd - src/common.c
 * Copyright (C) 2005  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Niki W. Waibel <niki.waibel@gmx.net>
**/

#include "common.h"
#include "utils_debug.h"

#ifdef HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
#endif

#ifdef HAVE_LIBRRD
static char *rra_def[] =
{
		"RRA:AVERAGE:0.2:6:1500",
		"RRA:AVERAGE:0.1:180:1680",
		"RRA:AVERAGE:0.1:2160:1520",
		"RRA:MIN:0.2:6:1500",
		"RRA:MIN:0.1:180:1680",
		"RRA:MIN:0.1:2160:1520",
		"RRA:MAX:0.2:6:1500",
		"RRA:MAX:0.1:180:1680",
		"RRA:MAX:0.1:2160:1520",
		NULL
};
static int rra_num = 9;
#endif /* HAVE_LIBRRD */

void
sstrncpy(char *d, const char *s, int len)
{
	strncpy(d, s, len);
	d[len - 1] = 0;
}

char *
sstrdup(const char *s)
{
	char *r = strdup(s);
	if(r == NULL) {
		DBG("Not enough memory.");
		exit(3);
	}
	return r;
}

void *
smalloc(size_t size)
{
	void *r = malloc(size);
	if(r == NULL) {
		DBG("Not enough memory.");
		exit(3);
	}
	return r;
}

int strsplit (char *string, char **fields, size_t size)
{
	size_t i;
	char *ptr;

	i = 0;
	ptr = string;
	while ((fields[i] = strtok (ptr, " \t")) != NULL)
	{
		ptr = NULL;
		i++;

		if (i >= size)
			break;
	}

	return (i);
}

#ifdef HAVE_LIBRRD
int check_create_dir (char *dir)
{
	struct stat statbuf;

	if (stat (dir, &statbuf) == -1)
	{
		if (errno == ENOENT)
		{
			if (mkdir (dir, 0755) == -1)
			{
				syslog (LOG_ERR, "mkdir (%s): %s", dir, strerror (errno));
				return (-1);
			}
		}
		else
		{
			syslog (LOG_ERR, "stat (%s): %s", dir, strerror (errno));
			return (-1);
		}
	}
	else if (!S_ISDIR (statbuf.st_mode))
	{
		syslog (LOG_ERR, "stat %s: Not a directory!", dir);
		return (-1);
	}

	return (0);
}

int rrd_create_file (char *filename, char **ds_def, int ds_num)
{
	char **argv;
	int argc;
	int i, j;
	int status = 0;

	argc = ds_num + rra_num + 4;

	if ((argv = (char **) malloc (sizeof (char *) * (argc + 1))) == NULL)
	{
		syslog (LOG_ERR, "rrd_create failed: %s", strerror (errno));
		return (-1);
	}

	argv[0] = "create";
	argv[1] = filename;
	argv[2] = "-s";
	argv[3] = "10";

	j = 4;
	for (i = 0; i < ds_num; i++)
		argv[j++] = ds_def[i];
	for (i = 0; i < rra_num; i++)
		argv[j++] = rra_def[i];
	argv[j] = NULL;

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();
	if (rrd_create (argc, argv) == -1)
	{
		syslog (LOG_ERR, "rrd_create failed: %s: %s", filename, rrd_get_error ());
		status = -1;
	}

	free (argv);
	
	return (status);
}
#endif /* HAVE_LIBRRD */

int rrd_update_file (char *host, char *file, char *values,
		char **ds_def, int ds_num)
{
#ifdef HAVE_LIBRRD
	struct stat statbuf;
	char full_file[1024];
	char *argv[4] = { "update", full_file, values, NULL };

	/* host == NULL => local mode */
	if (host != NULL)
	{
		if (check_create_dir (host))
			return (-1);

		if (snprintf (full_file, 1024, "%s/%s", host, file) >= 1024)
			return (-1);
	}
	else
	{
		if (snprintf (full_file, 1024, "%s", file) >= 1024)
			return (-1);
	}

	if (stat (full_file, &statbuf) == -1)
	{
		if (errno == ENOENT)
		{
			if (rrd_create_file (full_file, ds_def, ds_num))
				return (-1);
		}
		else
		{
			syslog (LOG_ERR, "stat %s: %s", full_file, strerror (errno));
			return (-1);
		}
	}
	else if (!S_ISREG (statbuf.st_mode))
	{
		syslog (LOG_ERR, "stat %s: Not a regular file!", full_file);
		return (-1);
	}

	optind = 0; /* bug in librrd? */
	rrd_clear_error ();
	if (rrd_update (3, argv) == -1)
	{
		syslog (LOG_WARNING, "rrd_update failed: %s: %s", full_file, rrd_get_error ());
		return (-1);
	}
#endif /* HAVE_LIBRRD */

	return (0);
}

#ifdef HAVE_LIBKSTAT
int get_kstat (kstat_t **ksp_ptr, char *module, int instance, char *name)
{
	char ident[128];
	
	if (kc == NULL)
		return (-1);

	snprintf (ident, 128, "%s,%i,%s", module, instance, name);
	ident[127] = '\0';

	if (*ksp_ptr == NULL)
	{
		if ((*ksp_ptr = kstat_lookup (kc, module, instance, name)) == NULL)
		{
			syslog (LOG_ERR, "Cound not find kstat %s", ident);
			return (-1);
		}

		if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED)
		{
			syslog (LOG_WARNING, "kstat %s has wrong type", ident);
			*ksp_ptr = NULL;
			return (-1);
		}
	}

#ifdef assert
	assert (*ksp_ptr != NULL);
	assert ((*ksp_ptr)->ks_type == KSTAT_TYPE_NAMED);
#endif

	if (kstat_read (kc, *ksp_ptr, NULL) == -1)
	{
		syslog (LOG_WARNING, "kstat %s could not be read", ident);
		return (-1);
	}

	if ((*ksp_ptr)->ks_type != KSTAT_TYPE_NAMED)
	{
		syslog (LOG_WARNING, "kstat %s has wrong type", ident);
		return (-1);
	}

	return (0);
}

long long get_kstat_value (kstat_t *ksp, char *name)
{
	kstat_named_t *kn;
	long long retval = -1LL;

#ifdef assert
	assert (ksp != NULL);
	assert (ksp->ks_type == KSTAT_TYPE_NAMED);
#else
	if (ksp == NULL)
	{
		fprintf (stderr, "ERROR: %s:%i: ksp == NULL\n", __FILE__, __LINE__);
		return (-1LL);
	}
	else if (ksp->ks_type != KSTAT_TYPE_NAMED)
	{
		fprintf (stderr, "ERROR: %s:%i: ksp->ks_type != KSTAT_TYPE_NAMED\n", __FILE__, __LINE__);
		return (-1LL);
	}
#endif

	if ((kn = (kstat_named_t *) kstat_data_lookup (ksp, name)) == NULL)
		return (retval);

	if (kn->data_type == KSTAT_DATA_INT32)
		retval = (long long) kn->value.i32;
	else if (kn->data_type == KSTAT_DATA_UINT32)
		retval = (long long) kn->value.ui32;
	else if (kn->data_type == KSTAT_DATA_INT64)
		retval = (long long) kn->value.i64; /* According to ANSI C99 `long long' must hold at least 64 bits */
	else if (kn->data_type == KSTAT_DATA_UINT64)
		retval = (long long) kn->value.ui64; /* XXX: Might overflow! */
	else
		syslog (LOG_WARNING, "get_kstat_value: Not a numeric value: %s", name);
		 
	return (retval);
}
#endif /* HAVE_LIBKSTAT */