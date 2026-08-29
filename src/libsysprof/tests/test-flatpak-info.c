/* test-flatpak-info.c
 *
 * Copyright 2026 Christian Hergert
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

#include <sys/stat.h>

#include "sysprof-elf-private.h"
#include "sysprof-elf-loader-private.h"
#include "sysprof-mount-namespace-private.h"
#include "sysprof-strings-private.h"

static void
test_flatpak_sdk (void)
{
  static const char contents[] =
    "[Application]\n"
    "name=org.gnome.Example\n"
    "runtime=org.gnome.Sdk/x86_64/50\n"
    "\n"
    "[Instance]\n"
    "app-path=/opt/flatpak/app/org.gnome.Example/x86_64/main/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/files\n"
    "app-commit=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"
    "app-extensions=org.gnome.Example.Debug="
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
    "runtime-path=/opt/flatpak/runtime/org.gnome.Sdk/x86_64/50/"
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc/files\n"
    "runtime-commit=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n"
    "runtime-extensions=org.gnome.Sdk.Debug="
      "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\n"
    "arch=x86_64\n"
    "branch=main\n";
  g_autoptr(SysprofMountNamespace) mount_namespace = NULL;
  SysprofStrings *strings;
  g_auto(GStrv) paths = NULL;

  mount_namespace = sysprof_mount_namespace_new ();
  strings = sysprof_strings_new ();

  sysprof_mount_namespace_add_flatpak (mount_namespace, strings, contents, strlen (contents));

  paths = sysprof_mount_namespace_translate (mount_namespace,
                                             "/usr/lib/debug/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[0], ==,
                   "/opt/flatpak/runtime/org.gnome.Sdk.Debug/x86_64/50/"
                   "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd/"
                   "files/usr/lib/libgtk-4.so.debug");

  g_clear_pointer (&paths, g_strfreev);
  paths = sysprof_mount_namespace_translate (mount_namespace, "/usr/lib/libgtk-4.so");
  g_assert_cmpstr (paths[0], ==,
                   "/opt/flatpak/runtime/org.gnome.Sdk/x86_64/50/"
                   "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc/"
                   "files/lib/libgtk-4.so");

  g_clear_pointer (&paths, g_strfreev);
  paths = sysprof_mount_namespace_translate (mount_namespace, "/app/bin/example");
  g_assert_cmpstr (paths[0], ==,
                   "/opt/flatpak/app/org.gnome.Example/x86_64/main/"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"
                   "files/bin/example");

  g_clear_pointer (&paths, g_strfreev);
  paths = sysprof_mount_namespace_translate (mount_namespace,
                                             "/app/lib/debug/app/bin/example.debug");
  g_assert_cmpstr (paths[0], ==,
                   "/opt/flatpak/runtime/org.gnome.Example.Debug/x86_64/main/"
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
                   "files/app/bin/example.debug");

  sysprof_strings_unref (strings);
}

static void
test_flatpak_platform (void)
{
  static const char contents[] =
    "[Application]\n"
    "name=org.gnome.Example\n"
    "runtime=org.gnome.Platform/aarch64/50\n"
    "\n"
    "[Instance]\n"
    "runtime-path=/srv/flatpak/runtime/org.gnome.Platform/aarch64/50/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/files\n"
    "arch=aarch64\n";
  g_autoptr(SysprofMountNamespace) mount_namespace = NULL;
  SysprofStrings *strings;
  g_auto(GStrv) paths = NULL;

  mount_namespace = sysprof_mount_namespace_new ();
  strings = sysprof_strings_new ();

  sysprof_mount_namespace_add_flatpak (mount_namespace, strings, contents, strlen (contents));

  paths = sysprof_mount_namespace_translate (mount_namespace,
                                             "/usr/lib/debug/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[0], ==,
                   "/srv/flatpak/runtime/org.gnome.Sdk.Debug/aarch64/50/active/"
                   "files/usr/lib/libgtk-4.so.debug");

  sysprof_strings_unref (strings);
}

static void
test_flatpak_ostree_home (void)
{
  static const char contents[] =
    "[Application]\n"
    "name=org.gnome.Example\n"
    "runtime=org.gnome.Sdk/x86_64/50\n"
    "\n"
    "[Instance]\n"
    "runtime-path=/var/home/alice/.local/share/flatpak/runtime/org.gnome.Sdk/x86_64/50/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/files\n"
    "runtime-extensions=org.gnome.Sdk.Debug="
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n"
    "arch=x86_64\n";
  g_autoptr(SysprofMountNamespace) mount_namespace = NULL;
  SysprofStrings *strings;
  g_auto(GStrv) paths = NULL;

  mount_namespace = sysprof_mount_namespace_new ();
  strings = sysprof_strings_new ();

  sysprof_mount_namespace_add_flatpak (mount_namespace, strings, contents, strlen (contents));

  paths = sysprof_mount_namespace_translate (mount_namespace,
                                             "/usr/lib/debug/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[0], ==,
                   "/var/home/alice/.local/share/flatpak/runtime/"
                   "org.gnome.Sdk.Debug/x86_64/50/"
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
                   "files/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[1], ==,
                   "/home/alice/.local/share/flatpak/runtime/"
                   "org.gnome.Sdk.Debug/x86_64/50/"
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
                   "files/usr/lib/libgtk-4.so.debug");

  sysprof_strings_unref (strings);
}

static void
test_overlay_order (void)
{
  g_autoptr(SysprofMountNamespace) mount_namespace = NULL;
  SysprofStrings *strings;
  g_auto(GStrv) paths = NULL;

  mount_namespace = sysprof_mount_namespace_new ();
  strings = sysprof_strings_new ();

  sysprof_mount_namespace_add_mount (mount_namespace,
                                     _sysprof_mount_new_for_overlay (strings,
                                                                     "/usr",
                                                                     "/layer-two",
                                                                     2));
  sysprof_mount_namespace_add_mount (mount_namespace,
                                     _sysprof_mount_new_for_overlay (strings,
                                                                     "/usr",
                                                                     "/layer-zero",
                                                                     0));
  sysprof_mount_namespace_add_mount (mount_namespace,
                                     _sysprof_mount_new_for_overlay (strings,
                                                                     "/usr",
                                                                     "/layer-one",
                                                                     1));

  paths = sysprof_mount_namespace_translate (mount_namespace, "/usr/lib/libgtk-4.so");
  g_assert_cmpstr (paths[0], ==, "/layer-zero/lib/libgtk-4.so");
  g_assert_cmpstr (paths[1], ==, "/layer-one/lib/libgtk-4.so");
  g_assert_cmpstr (paths[2], ==, "/layer-two/lib/libgtk-4.so");

  sysprof_strings_unref (strings);
}

static void
test_btrfs_ostree_paths (void)
{
  static const char flatpak_mountinfo[] =
    "101 100 0:42 /home/alice/.local/share/flatpak/runtime/org.gnome.Sdk/x86_64/50/"
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/files "
    "/usr ro - btrfs /dev/mapper/fedora rw,subvol=/home";
  static const char sysroot_mountinfo[] =
    "102 100 0:42 /root/ostree/deploy/fedora/deploy/deadbeef.0/var/lib/flatpak/runtime/"
    "org.gnome.Sdk.Debug/x86_64/50/"
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/files "
    "/usr/lib/debug ro - btrfs /dev/mapper/fedora rw,subvol=/root";
  g_autoptr(SysprofMountNamespace) mount_namespace = NULL;
  SysprofStrings *strings;
  g_auto(GStrv) paths = NULL;

  mount_namespace = sysprof_mount_namespace_new ();
  strings = sysprof_strings_new ();

  sysprof_mount_namespace_add_device (
    mount_namespace,
    sysprof_mount_device_new (sysprof_strings_get (strings, "/dev/mapper/fedora"),
                              sysprof_strings_get (strings, "/var/home"),
                              sysprof_strings_get (strings, "/home")));
  sysprof_mount_namespace_add_device (
    mount_namespace,
    sysprof_mount_device_new (sysprof_strings_get (strings, "/dev/mapper/fedora"),
                              sysprof_strings_get (strings, "/home"),
                              sysprof_strings_get (strings, "/home")));
  sysprof_mount_namespace_add_device (
    mount_namespace,
    sysprof_mount_device_new (sysprof_strings_get (strings, "/dev/mapper/fedora"),
                              sysprof_strings_get (strings, "/sysroot"),
                              sysprof_strings_get (strings, "/root")));
  sysprof_mount_namespace_add_device (
    mount_namespace,
    sysprof_mount_device_new (sysprof_strings_get (strings, "/dev/mapper/fedora"),
                              sysprof_strings_get (strings, "/var"),
                              sysprof_strings_get (strings, "/root")));
  sysprof_mount_namespace_add_device (
    mount_namespace,
    sysprof_mount_device_new (sysprof_strings_get (strings, "/dev/mapper/fedora"),
                              sysprof_strings_get (strings, "/mnt/btrfs"),
                              sysprof_strings_get (strings, "/")));

  sysprof_mount_namespace_add_mount (
    mount_namespace,
    _sysprof_mount_new_for_mountinfo (strings, flatpak_mountinfo));
  sysprof_mount_namespace_add_mount (
    mount_namespace,
    _sysprof_mount_new_for_mountinfo (strings, sysroot_mountinfo));

  paths = sysprof_mount_namespace_translate (mount_namespace, "/usr/lib/libgtk-4.so");
  g_assert_cmpstr (paths[0], ==,
                   "/var/home/alice/.local/share/flatpak/runtime/org.gnome.Sdk/x86_64/50/"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"
                   "files/lib/libgtk-4.so");
  g_assert_cmpstr (paths[1], ==,
                   "/home/alice/.local/share/flatpak/runtime/org.gnome.Sdk/x86_64/50/"
                   "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/"
                   "files/lib/libgtk-4.so");

  g_clear_pointer (&paths, g_strfreev);
  paths = sysprof_mount_namespace_translate (mount_namespace,
                                             "/usr/lib/debug/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[0], ==,
                   "/sysroot/ostree/deploy/fedora/deploy/deadbeef.0/var/lib/flatpak/"
                   "runtime/org.gnome.Sdk.Debug/x86_64/50/"
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
                   "files/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[1], ==,
                   "/var/ostree/deploy/fedora/deploy/deadbeef.0/var/lib/flatpak/"
                   "runtime/org.gnome.Sdk.Debug/x86_64/50/"
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
                   "files/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[2], ==,
                   "/mnt/btrfs/root/ostree/deploy/fedora/deploy/deadbeef.0/var/lib/flatpak/"
                   "runtime/org.gnome.Sdk.Debug/x86_64/50/"
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
                   "files/usr/lib/libgtk-4.so.debug");

  sysprof_strings_unref (strings);
}

static void
test_elf_matching (void)
{
  g_autoptr(GMappedFile) mapped_file = NULL;
  g_autoptr(SysprofElf) elf = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *filename = NULL;
  const char *build_id;
  struct stat stbuf;

  filename = g_file_read_link ("/proc/self/exe", &error);
  g_assert_no_error (error);
  g_assert_nonnull (filename);
  g_assert_cmpint (stat (filename, &stbuf), ==, 0);

  mapped_file = g_mapped_file_new (filename, FALSE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (mapped_file);

  elf = sysprof_elf_new (filename,
                         g_steal_pointer (&mapped_file),
                         stbuf.st_ino,
                         &error);
  g_assert_no_error (error);
  g_assert_nonnull (elf);

  build_id = sysprof_elf_get_build_id (elf);
  g_assert_nonnull (build_id);
  g_assert_true (sysprof_elf_matches (elf, stbuf.st_ino + 1, build_id));
  g_assert_false (sysprof_elf_matches (elf,
                                      stbuf.st_ino,
                                      "0000000000000000000000000000000000000000"));
  g_assert_true (sysprof_elf_matches (elf, stbuf.st_ino, NULL));
  g_assert_false (sysprof_elf_matches (elf, stbuf.st_ino + 1, NULL));
}

static void
test_flatpak_prefixed_runtime (void)
{
  static const char contents[] =
    "[Application]\n"
    "name=org.gnome.Example\n"
    "runtime=runtime/org.gnome.Sdk/x86_64/50\n"
    "\n"
    "[Instance]\n"
    "runtime-path=/opt/flatpak/runtime/org.gnome.Sdk/x86_64/50/"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/files\n"
    "runtime-extensions=org.gnome.Sdk.Debug="
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n";
  g_autoptr(SysprofMountNamespace) mount_namespace = NULL;
  SysprofStrings *strings;
  g_auto(GStrv) paths = NULL;

  mount_namespace = sysprof_mount_namespace_new ();
  strings = sysprof_strings_new ();

  sysprof_mount_namespace_add_flatpak (mount_namespace, strings, contents, strlen (contents));

  paths = sysprof_mount_namespace_translate (mount_namespace,
                                             "/usr/lib/debug/usr/lib/libgtk-4.so.debug");
  g_assert_cmpstr (paths[0], ==,
                   "/opt/flatpak/runtime/org.gnome.Sdk.Debug/x86_64/50/"
                   "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb/"
                   "files/usr/lib/libgtk-4.so.debug");

  sysprof_strings_unref (strings);
}

static void
test_build_id_path (void)
{
  g_autofree char *path = NULL;

  path = _sysprof_elf_loader_build_id_path ("/usr/lib/debug", "abcdef0123456789");
  g_assert_cmpstr (path, ==, "/usr/lib/debug/.build-id/ab/cdef0123456789.debug");
}

static void
test_flatpak_access_paths (void)
{
  g_autofree char *path = NULL;

  path = _sysprof_elf_loader_access_path ("/var/home/alice/.local/lib/example.so",
                                          TRUE,
                                          FALSE);
  g_assert_null (path);

  path = _sysprof_elf_loader_access_path ("/sysroot/ostree/deploy/example.so",
                                          TRUE,
                                          FALSE);
  g_assert_null (path);

  path = _sysprof_elf_loader_access_path ("/var/lib/flatpak/runtime/example.so",
                                          TRUE,
                                          FALSE);
  g_assert_null (path);

  path = _sysprof_elf_loader_access_path ("/usr/lib/example.so", TRUE, FALSE);
  g_assert_cmpstr (path, ==, "/var/run/host/usr/lib/example.so");

  g_clear_pointer (&path, g_free);
  path = _sysprof_elf_loader_access_path ("/var/home/alice/example.so", FALSE, TRUE);
  g_assert_cmpstr (path, ==, "/var/run/host/var/home/alice/example.so");
}

int
main (int   argc,
      char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/Sysprof/Flatpak/sdk", test_flatpak_sdk);
  g_test_add_func ("/Sysprof/Flatpak/platform", test_flatpak_platform);
  g_test_add_func ("/Sysprof/Flatpak/ostree-home", test_flatpak_ostree_home);
  g_test_add_func ("/Sysprof/Flatpak/prefixed-runtime", test_flatpak_prefixed_runtime);
  g_test_add_func ("/Sysprof/Mount/overlay-order", test_overlay_order);
  g_test_add_func ("/Sysprof/Mount/btrfs-ostree-paths", test_btrfs_ostree_paths);
  g_test_add_func ("/Sysprof/Elf/matching", test_elf_matching);
  g_test_add_func ("/Sysprof/Elf/build-id-path", test_build_id_path);
  g_test_add_func ("/Sysprof/Elf/flatpak-access-paths", test_flatpak_access_paths);

  return g_test_run ();
}
