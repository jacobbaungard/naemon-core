#include <string.h>
#include "config.h"
#include "common.h"
#include "objects.h"
#include "objects_common.h"
#include "objects_hostdependency.h"
#include "objects_hostescalation.h"
#include "objects_servicedependency.h"
#include "objects_serviceescalation.h"
#include "objects_timeperiod.h"
#include "logging.h"
#include "globals.h"
#include "nm_alloc.h"

int __nagios_object_structure_version = CURRENT_OBJECT_STRUCTURE_VERSION;

/* writes cached object definitions for use by web interface */
int fcache_objects(char *cache_file)
{
	FILE *fp = NULL;
	time_t current_time = 0L;
	unsigned int i;

	/* some people won't want to cache their objects */
	if (!cache_file || !strcmp(cache_file, "/dev/null"))
		return OK;

	time(&current_time);

	/* open the cache file for writing */
	fp = fopen(cache_file, "w");
	if (fp == NULL) {
		nm_log(NSLOG_CONFIG_WARNING, "Warning: Could not open object cache file '%s' for writing!\n", cache_file);
		return ERROR;
	}

	/* write header to cache file */
	fprintf(fp, "########################################\n");
	fprintf(fp, "#       NAGIOS OBJECT CACHE FILE\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# THIS FILE IS AUTOMATICALLY GENERATED\n");
	fprintf(fp, "# BY NAGIOS.  DO NOT MODIFY THIS FILE!\n");
	fprintf(fp, "#\n");
	fprintf(fp, "# Created: %s", ctime(&current_time));
	fprintf(fp, "########################################\n\n");


	/* cache timeperiods */
	for (i = 0; i < num_objects.timeperiods; i++)
		fcache_timeperiod(fp, timeperiod_ary[i]);

	/* cache commands */
	for (i = 0; i < num_objects.commands; i++)
		fcache_command(fp, command_ary[i]);

	/* cache contactgroups */
	for (i = 0; i < num_objects.contactgroups; i++)
		fcache_contactgroup(fp, contactgroup_ary[i]);

	/* cache hostgroups */
	for (i = 0; i < num_objects.hostgroups; i++)
		fcache_hostgroup(fp, hostgroup_ary[i]);

	/* cache servicegroups */
	for (i = 0; i < num_objects.servicegroups; i++)
		fcache_servicegroup(fp, servicegroup_ary[i]);

	/* cache contacts */
	for (i = 0; i < num_objects.contacts; i++)
		fcache_contact(fp, contact_ary[i]);

	/* cache hosts */
	for (i = 0; i < num_objects.hosts; i++)
		fcache_host(fp, host_ary[i]);

	/* cache services */
	for (i = 0; i < num_objects.services; i++)
		fcache_service(fp, service_ary[i]);

	/* cache service dependencies */
	for (i = 0; i < num_objects.services; i++) {
		struct objectlist *deplist;
		for (deplist = service_ary[i]->exec_deps; deplist; deplist = deplist->next)
			fcache_servicedependency(fp, deplist->object_ptr);
		for (deplist = service_ary[i]->notify_deps; deplist; deplist = deplist->next)
			fcache_servicedependency(fp, deplist->object_ptr);
	}

	/* cache service escalations */
	for (i = 0; i < num_objects.services; i++) {
		struct objectlist *esclist;
		for (esclist = service_ary[i]->escalation_list; esclist; esclist = esclist->next)
			fcache_serviceescalation(fp, esclist->object_ptr);
	}

	/* cache host dependencies */
	for (i = 0; i < num_objects.hosts; i++) {
		struct objectlist *deplist;
		for (deplist = host_ary[i]->exec_deps; deplist; deplist = deplist->next)
			fcache_hostdependency(fp, deplist->object_ptr);
		for (deplist = host_ary[i]->notify_deps; deplist; deplist = deplist->next)
			fcache_hostdependency(fp, deplist->object_ptr);
	}

	/* cache host escalations */
	for (i = 0; i < num_objects.hosts; i++) {
		struct objectlist *esclist;
		for (esclist = host_ary[i]->escalation_list; esclist; esclist = esclist->next)
			fcache_hostescalation(fp, esclist->object_ptr);
	}

	fclose(fp);

	return OK;
}
