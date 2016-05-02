/*
   Copyright 2016 Facebook

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <fts.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <errno.h>


guint64 chunk = 128;            /* Megabytes */

double sleep_time = 0.1;

/* most important counter */
static int the_counter = 0;
/* which gets forgotten by dreaming */
void dream()
{
    /* sleep override */
    usleep(sleep_time * 1000000);
    the_counter = 0;
}

int recursive;
int force;
int onefs;
gchar **paths;

static GOptionEntry entries[] = {
    {"recursive", 'r', 0, G_OPTION_ARG_NONE, &recursive,
     "Dive into directories recursively", NULL},
    {"chunk", 'c', 0, G_OPTION_ARG_INT, &chunk, "Chunk size in megabytes",
     "128"},
    {"sleep", 's', 0, G_OPTION_ARG_DOUBLE, &sleep_time,
     "Sleep time between chunks", "0.1"},
    {"force", 'f', 0, G_OPTION_ARG_NONE, &force,
     "Continue on errors (by default bail on everything)", NULL},
    {"one-file-system", 'x', 0, G_OPTION_ARG_NONE, &onefs,
     "Only operate on one file system", NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, &paths},
    {NULL}
};

gboolean unlink_entry(FTSENT * e)
{
    if (unlink(e->fts_accpath) < 0) {
        g_printerr("Could not unlink (%s): %s\n", e->fts_path,
                   strerror(errno));
        if (force)
            return FALSE;
        else
            exit(EXIT_FAILURE);
    }
    return TRUE;
}


int main(int ac, char **av)
{
    GError *error = NULL;
    GOptionContext *context = g_option_context_new("PATH [PATH] ...");

    g_option_context_add_main_entries(context, entries, NULL);
    if (!g_option_context_parse(context, &ac, &av, &error)) {
        g_printerr("option parsing failed: %s\n", error->message);
        exit(EXIT_FAILURE);
    }

    if (!paths) {
        g_printerr("Please provide at least one path.\n");
        exit(EXIT_FAILURE);
    }

    /* Megabytize the argument */
    if (chunk)
        chunk <<= 20;

    FTS *fts = fts_open(paths, onefs ? FTS_XDEV : 0, NULL);
    FTSENT *e;

    while ((e = fts_read(fts))) {
        switch (e->fts_info) {
        case FTS_D:
            /* Directories are easiest,
             * only dealwith them after we cleaned them
             * We bail though, if we encounter directory in non-recursive mode
             */
            if (!recursive) {
                g_printerr("Directory (%s) encountered "
                           "in non-recursive mode.\n", e->fts_path);
                if (force) {
                    /* We skip the sub-tree and continue to next file */
                    fts_set(fts, e, FTS_SKIP);
                } else {
                    exit(EXIT_FAILURE);
                }
            }
            break;
        case FTS_DP:
            if (rmdir(e->fts_accpath) < 0) {
                g_printerr("Could not remove (%s) directory: %s\n",
                           e->fts_path, strerror(errno));
                if (!force)
                    exit(EXIT_FAILURE);
            }
            break;
        case FTS_F:
            /* Handling regular files. We sleep when we have
             * many smalls with large cumulative size.
             *
             * We also try to chunk large files and truncate
             * before dropping the fd
             *
             * If we're not running in force mode we
             * will bail pretty much all the time.
             *
             * Otherwise we will keep doing everything.
             */
            if (the_counter > chunk)
                dream();

            if (e->fts_statp->st_size > chunk && e->fts_statp->st_nlink <= 1) {
                /* Large file case.
                 * We only do this for files
                 * that are not hardlinked anywhere else.
                 */
                int fd = open(e->fts_accpath, O_RDWR);
                if (fd < 0) {
                    g_printerr("Could not open (%s) for truncation: %s",
                               e->fts_path, strerror(errno));
                    if (!force)
                        exit(EXIT_FAILURE);
                    break;
                }

                if (!unlink_entry(e))
                    break;

                off_t boundary = e->fts_statp->st_size;
                /* We don't care about sparseness of the
                 * file and approach this as logical trim */
                while (boundary >= chunk) {
                    boundary -= chunk;
                    if (ftruncate(fd, boundary) < 0) {
                        g_printerr("Could not truncate (%s): %s\n",
                                   e->fts_path, strerror(errno));
                        if (!force)
                            exit(EXIT_FAILURE);
                        break;
                    }
                    dream();
                }
                if (fd > -1)
                    close(fd);

            } else {
                /* Small file case */
                if (unlink_entry(e))
                    the_counter += e->fts_statp->st_size;
            }
            break;
        default:
            /* Everything else */
            unlink_entry(e);
        }
    }

    if (fts)
        fts_close(fts);

    return EXIT_SUCCESS;
}
