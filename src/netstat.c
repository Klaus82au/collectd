/**
     * collectd - src/netstat.c
     * Copyright (C) 2017  Микола Фарима
     *
     * This program is free software; you can redistribute it and/or modify it
     * under the terms of the GNU General Public License as published by the
     * Free Software Foundation; only version 2 of the License is applicable.
     *
     * This program is distributed in the hope that it will be useful, but
     * WITHOUT ANY WARRANTY; without even the implied warranty of
     * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     * General Public License for more details.
     *
     * You should have received a copy of the GNU General Public License along
     * with this program; if not, write to the Free Software Foundation, Inc.,
     * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
     *
     * Authors:
     *   Микола Фарима
     **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <unistd.h>
#include <dirent.h>
#include <stdio.h>

#define PATH_SEPARATOR "/"

#define MAX_BUFF 24


static char *dir;
static char *file;


struct netdevlist_s {
    char *name;
    int value;

    struct netdevlist_s *next;
};

typedef struct netdevlist_s netdevlist_t;

static netdevlist_t * netdevlist_head = NULL;


static const char *config_keys[] = {"NetDirPath", "Statistics"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);



//creates a netdevlist_t node for every directory in @d
//ignores .. and .
//returns number of created items
static int get_interfaces(DIR* d)
{
    struct dirent *dir_ent;
    int count = 0;
    while ((dir_ent = readdir(d))!=NULL)
    {
        if ( strcmp(dir_ent->d_name, ".") && strcmp(dir_ent->d_name, "..") ) {
            netdevlist_t *dl;

            if((dl = malloc(sizeof(*dl)))==NULL){
                ERROR("Failed to allocate");
                return -1;
            }
            dl->name=strdup(dir_ent->d_name);
            count++;
            dl->next = netdevlist_head;
            netdevlist_head = dl;
        }
    }
    closedir(d);
    return count;
}

static int is_dir(const char *path) {
    struct stat s;
    if (lstat(path, &s) == -1) {
        /* return as false */
        return 0;
    }
    return S_ISDIR(s.st_mode);
}

static int netstat_config(const char *key, const char *value)
{
    if (strcasecmp(key, "NetDirPath") == 0) {
        if (!is_dir(value)){
            ERROR("%s is not a valid directory!", value);
        }
        dir = sstrdup(value);
    } else if (strcasecmp(key, "Statistics") == 0) {
        file = sstrdup(value);
    } else {
        ERROR("Invalid netstat config key %s", key);
        return -1;
    }
    return 0;
}

static int netstat_init(void)
{
    INFO("netstat started");
    DIR           *d;
    if ((d = opendir(dir)) == NULL){
        ERROR("Can't open directory %s", dir);
        return -1;
    }

    if (get_interfaces(d)==0) {
        WARNING("No interfaces found");
    }

    INFO("Network interfaces found:");
    for (netdevlist_t *dl = netdevlist_head; dl != NULL; dl = dl->next){
        INFO("%s", dl->name);
    }
    return 0;
}

static void netstat_submit(char * devname, gauge_t value){
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = &(value_t){.gauge = value};
    vl.values_len = 1;
    sstrncpy(vl.plugin, "netstat", sizeof(vl.plugin));
    sstrncpy(vl.type_instance, devname, sizeof(vl.type_instance));
    sstrncpy(vl.type, "newtype", sizeof(vl.type));
    plugin_dispatch_values(&vl);
}

static int netstat_shutdown(void)
{
    INFO("netstat shutdown");
    netdevlist_t *dl;
    dl = netdevlist_head;
    while (dl != NULL) {
        netdevlist_t *dl_next;

        dl_next = dl->next;

        sfree(dl->name);
        sfree(dl);

        dl = dl_next;
    }
    if (dir) {
        sfree(dir);
    }
    if (file) {
        sfree(file);
    }
    return 0;
}

static int netstat_read(void) {
    INFO("IT's alive with PID: %d", getpid());
    for (netdevlist_t *dl = netdevlist_head; dl != NULL; dl = dl->next)
    {

        char * dir_stat = ssnprintf_alloc("%s%s/statistics/%s", dir, dl->name, file);
        INFO("DIR_STAT = %s", dir_stat);

        int input_fd = open(dir_stat, O_RDONLY|O_NONBLOCK);
        if (input_fd == -1) {
            ERROR("Can not open stats file %s", dir_stat);
            return -1;
        }
        char buff[MAX_BUFF] = {0};
        if((read(input_fd, buff, MAX_BUFF))==-1){
            ERROR("Error reading stats file");
        }
        dl->value=atoi(buff);
        WARNING("Value: %d", dl->value);
        netstat_submit(dl->name, dl->value);
        free(dir_stat);
    }
    return 0;
}

static int netstat_notification(const notification_t *n,
                                  user_data_t __attribute__((unused)) *
                                  user_data) {

    DEBUG("netstat NOTIFICATION!!!");
    INFO("Message: %s", n->message);
    return 0;
}

void module_register(void) {
    plugin_register_config("netstat", netstat_config, config_keys, config_keys_num);
    plugin_register_read("netstat", netstat_read);
    plugin_register_init("netstat", netstat_init);
    plugin_register_shutdown("netstat", netstat_shutdown);
    plugin_register_notification ("netstat",
                                  netstat_notification, NULL);
} /* void module_register */
