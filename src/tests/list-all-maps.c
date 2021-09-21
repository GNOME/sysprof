/* list-all-maps.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

#include <sysprof.h>
#include <stdio.h>

#include "../libsysprof/sysprof-helpers.h"
#include "../libsysprof/sysprof-mountinfo.h"
#include "../libsysprof/sysprof-path-resolver.h"
#include "../libsysprof/sysprof-podman.h"

typedef enum {
  PROCESS_KIND_HOST,
  PROCESS_KIND_FLATPAK,
  PROCESS_KIND_PODMAN,
} ProcessKind;

typedef struct
{
  ProcessKind kind : 2;
  int pid : 29;
  char *kind_identifier;
  char *mountinfo;
  char *comm;
  GArray *maps;
  GArray *overlays;
} ProcessInfo;

typedef struct
{
  char *on_host;
  char *in_process;
} ProcessOverlay;

typedef struct
{
  char *file;
  SysprofCaptureAddress start;
  SysprofCaptureAddress end;
  SysprofCaptureAddress offset;
  ino_t inode;
} ProcessMap;

static const char *
kind_to_string (ProcessKind kind)
{
  switch (kind)
    {
    case PROCESS_KIND_HOST:
      return "Host";
    case PROCESS_KIND_PODMAN:
      return "Podman";
    case PROCESS_KIND_FLATPAK:
      return "Flatpak";
    default:
      return "Unknown";
    }
}

static void
process_overlay_clear (ProcessOverlay *overlay)
{
  g_free (overlay->on_host);
  g_free (overlay->in_process);
}

static void
process_info_clear (ProcessInfo *info)
{
  g_free (info->kind_identifier);
  g_free (info->mountinfo);
  g_free (info->comm);
  g_clear_pointer (&info->maps, g_array_unref);
  g_clear_pointer (&info->overlays, g_array_unref);
}

static void
process_map_clear (ProcessMap *map)
{
  g_free (map->file);
}

static ProcessKind
get_process_kind_from_cgroup (int          pid,
                              const char  *cgroup,
                              char       **identifier)
{
  static GRegex *podman_regex;
  static GRegex *flatpak_regex;

  g_autoptr(GMatchInfo) podman_match = NULL;
  g_autoptr(GMatchInfo) flatpak_match = NULL;

  if G_UNLIKELY (podman_regex == NULL)
    podman_regex = g_regex_new ("libpod-([a-z0-9]{64})\\.scope", G_REGEX_OPTIMIZE, 0, NULL);

  if G_UNLIKELY (flatpak_regex == NULL)
    flatpak_regex = g_regex_new ("app-flatpak-([a-zA-Z_\\-\\.]+)-[0-9]+\\.scope", G_REGEX_OPTIMIZE, 0, NULL);

  if (g_regex_match (podman_regex, cgroup, 0, &podman_match))
    {
      if (identifier != NULL)
        *identifier = g_match_info_fetch (podman_match, 1);
      return PROCESS_KIND_PODMAN;
    }
  else if (g_regex_match (flatpak_regex, cgroup, 0, &flatpak_match))
    {
      if (identifier != NULL)
        *identifier = g_match_info_fetch (flatpak_match, 1);
      return PROCESS_KIND_FLATPAK;
    }
  else
    {
      if (identifier != NULL)
        *identifier = NULL;
      return PROCESS_KIND_HOST;
    }
}

static void
process_info_populate_podman_overlays (ProcessInfo   *proc,
                                       SysprofPodman *podman)
{
  g_auto(GStrv) layers = NULL;

  g_assert (proc != NULL);
  g_assert (proc->kind == PROCESS_KIND_PODMAN);

  if ((layers = sysprof_podman_get_layers (podman, proc->kind_identifier)))
    {
      for (guint i = 0; layers[i]; i++)
        {
          ProcessOverlay overlay;
          g_autofree char *path = g_build_filename (g_get_user_data_dir (),
                                                    "containers",
                                                    "storage",
                                                    "overlay",
                                                    layers[i],
                                                    "diff",
                                                    NULL);

          /* XXX: this really only supports one layer */
          overlay.in_process = g_strdup ("/");
          overlay.on_host = g_steal_pointer (&path);

          if (proc->overlays == NULL)
            {
              proc->overlays = g_array_new (FALSE, FALSE, sizeof (ProcessOverlay));
              g_array_set_clear_func (proc->overlays, (GDestroyNotify)process_overlay_clear);
            }

          g_array_append_val (proc->overlays, overlay);
        }
    }
}

static void
process_info_populate_maps (ProcessInfo *proc,
                            const char  *maps)
{
  g_auto(GStrv) lines = NULL;

  g_assert (proc != NULL);
  g_assert (maps != NULL);

  if (proc->maps == NULL)
    {
      proc->maps = g_array_new (FALSE, FALSE, sizeof (ProcessMap));
      g_array_set_clear_func (proc->maps, (GDestroyNotify)process_map_clear);
    }

  lines = g_strsplit (maps, "\n", 0);

  for (guint i = 0; lines[i] != NULL; i++)
    {
      ProcessMap map;
      gchar file[512];
      gulong start;
      gulong end;
      gulong offset;
      gulong inode;
      int r;

      r = sscanf (lines[i],
                  "%lx-%lx %*15s %lx %*x:%*x %lu %512s",
                  &start, &end, &offset, &inode, file);
      file[sizeof file - 1] = '\0';

      if (r != 5)
        continue;

      if (strcmp ("[vdso]", file) == 0)
        {
          /*
           * Søren Sandmann Pedersen says:
           *
           * For the vdso, the kernel reports 'offset' as the
           * the same as the mapping address. This doesn't make
           * any sense to me, so we just zero it here. There
           * is code in binfile.c (read_inode) that returns 0
           * for [vdso].
           */
          offset = 0;
          inode = 0;
        }

      map.file = g_strdup (file);
      map.start = start;
      map.end = end;
      map.offset = offset;
      map.inode = inode;

      g_array_append_val (proc->maps, map);
    }
}

static gboolean
process_info_populate (ProcessInfo   *proc,
                       GVariant      *variant,
                       SysprofPodman *podman)
{
  const char *str;
  int pid;

  g_assert (proc != NULL);
  g_assert (proc->pid == 0);
  g_assert (variant != NULL);
  g_assert (g_variant_is_of_type (variant, G_VARIANT_TYPE_VARDICT));

  if (g_variant_lookup (variant, "pid", "i", &pid))
    proc->pid = pid;
  else
    return FALSE;

  if (g_variant_lookup (variant, "comm", "&s", &str))
    proc->comm = g_strdup (str);

  if (g_variant_lookup (variant, "cgroup", "&s", &str))
    proc->kind = get_process_kind_from_cgroup (pid, str, &proc->kind_identifier);
  else
    proc->kind = PROCESS_KIND_HOST;

  if (proc->kind == PROCESS_KIND_PODMAN)
    process_info_populate_podman_overlays (proc, podman);

  if (g_variant_lookup (variant, "mountinfo", "&s", &str))
    proc->mountinfo = g_strdup (str);

  if (g_variant_lookup (variant, "maps", "&s", &str))
    process_info_populate_maps (proc, str);

  return TRUE;
}

static gboolean
list_all_maps (GError **error)
{
  SysprofHelpers *helpers = sysprof_helpers_get_default ();
  g_autoptr(SysprofPodman) podman = NULL;
  g_autofree char *mounts = NULL;
  g_autoptr(GVariant) info = NULL;
  g_autoptr(GArray) processes = NULL;
  GVariant *value;
  GVariantIter iter;

  if (!sysprof_helpers_get_process_info (helpers, "pid,maps,mountinfo,cmdline,comm,cgroup", FALSE, NULL, &info, error))
    return FALSE;

  if (!sysprof_helpers_get_proc_file (helpers, "/proc/mounts", NULL, &mounts, error))
    return FALSE;

  podman = sysprof_podman_snapshot_current_user ();

  processes = g_array_new (FALSE, TRUE, sizeof (ProcessInfo));
  g_array_set_clear_func (processes, (GDestroyNotify)process_info_clear);

  g_variant_iter_init (&iter, info);
  while (g_variant_iter_loop (&iter, "@a{sv}", &value))
    {
      ProcessInfo *proc;

      g_array_set_size (processes, processes->len + 1);
      proc = &g_array_index (processes, ProcessInfo, processes->len - 1);

      if (!process_info_populate (proc, value, podman))
        g_array_set_size (processes, processes->len - 1);
    }

  for (guint i = 0; i < processes->len; i++)
    {
      const ProcessInfo *proc = &g_array_index (processes, ProcessInfo, i);
      g_autoptr(SysprofPathResolver) resolver = _sysprof_path_resolver_new (mounts, proc->mountinfo);

      if (proc->maps == NULL || proc->maps->len == 0)
        continue;

      g_print ("Process %d", proc->pid);
      if (proc->kind != PROCESS_KIND_HOST)
        g_print (" (%s)", kind_to_string (proc->kind));
      g_print (" %s\n", proc->comm);

      if (proc->overlays != NULL)
        {
          for (guint j = 0; j < proc->overlays->len; j++)
          {
            const ProcessOverlay *overlay = &g_array_index (proc->overlays, ProcessOverlay, j);
            _sysprof_path_resolver_add_overlay (resolver, overlay->in_process, overlay->on_host, j);
          }
        }

      for (guint j = 0; j < proc->maps->len; j++)
        {
          const ProcessMap *map = &g_array_index (proc->maps, ProcessMap, j);
          g_autofree char *path = _sysprof_path_resolver_resolve (resolver, map->file);

          if (path != NULL)
            {
              g_print ("  %s", path);
              if (!g_file_test (path, G_FILE_TEST_EXISTS))
                g_print (" (missing)");
              g_print ("\n");
            }
        }

      g_print ("\n");
    }

  return TRUE;
}

int
main (int   argc,
      char *argv[])
{
  g_autoptr(GError) error = NULL;
  if (!list_all_maps (&error))
    g_printerr ("%s\n", error->message);
  return error ? EXIT_FAILURE : EXIT_SUCCESS;
}
