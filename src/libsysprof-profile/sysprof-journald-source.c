/* sysprof-journald-source.c
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include <systemd/sd-journal.h>

#include "sysprof-journald-source.h"

typedef struct _SysprofJournaldSource
{
  GSource                    parent_instance;
  sd_journal                *journal;
  SysprofJournaldSourceFunc  func;
  gpointer                   func_data;
  GDestroyNotify             func_data_destroy;
} SysprofJournaldSource;

static void
sysprof_journald_source_finalize (GSource *source)
{
  SysprofJournaldSource *self = (SysprofJournaldSource *)source;

  if (self->func_data_destroy)
    self->func_data_destroy (self->func_data);

  g_clear_pointer (&self->journal, sd_journal_close);
}

static gboolean
sysprof_journald_source_dispatch (GSource     *source,
                                  GSourceFunc  callback,
                                  gpointer     user_data)
{
  SysprofJournaldSource *self = (SysprofJournaldSource *)source;

  if (sd_journal_process (self->journal) == SD_JOURNAL_APPEND)
    {
      if (sd_journal_next (self->journal) > 0)
        return self->func (self->func_data, self->journal);
    }

  return G_SOURCE_CONTINUE;
}

static GSourceFuncs sysprof_journald_source_funcs = {
  .dispatch = sysprof_journald_source_dispatch,
  .finalize = sysprof_journald_source_finalize,
};

GSource *
sysprof_journald_source_new (SysprofJournaldSourceFunc func,
                             gpointer                  func_data,
                             GDestroyNotify            func_data_destroy)
{
  SysprofJournaldSource *self;
  sd_journal *journal = NULL;
  int fd;

  if (sd_journal_open (&journal, 0) < 0)
    return NULL;

  if (sd_journal_seek_tail (journal) < 0)
    goto failure;

  if (sd_journal_previous (journal) < 0)
    goto failure;

  if (sd_journal_next (journal) < 0)
    goto failure;

  if (-1 == (fd = sd_journal_get_fd (journal)))
    goto failure;

  self = (SysprofJournaldSource *)g_source_new (&sysprof_journald_source_funcs,
                                                sizeof (SysprofJournaldSource));
  self->journal = journal;
  self->func = func;
  self->func_data = func_data;
  self->func_data_destroy = func_data_destroy;

  g_source_add_unix_fd ((GSource *)self, fd, sd_journal_get_events (journal));

  return (GSource *)self;

failure:
  g_clear_pointer (&journal, sd_journal_close);

  return NULL;
}
