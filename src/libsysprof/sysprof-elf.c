/* sysprof-elf.c
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

#include "elfparser.h"

#include "sysprof-elf-private.h"

struct _SysprofElf
{
  GObject parent_instance;
  const char *nick;
  char *build_id;
  char *file;
  SysprofElf *debug_link_elf;
  ElfParser *parser;
  guint64 file_inode;
  gulong text_offset;
};

enum {
  PROP_0,
  PROP_BUILD_ID,
  PROP_DEBUG_LINK,
  PROP_DEBUG_LINK_ELF,
  PROP_FILE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SysprofElf, sysprof_elf, G_TYPE_OBJECT)

static GParamSpec *properties [N_PROPS];
static GHashTable *nicks;
static const struct {
  const char *library;
  const char *nick;
} nick_table[] = {
  /* Self Tracking Sysprof */
  { "libsysprof-memory-6.so", "Sysprof" },
  { "libsysprof-tracer-6.so", "Sysprof" },
  { "libsysprof-6.so", "Sysprof" },

  /* Platform */
  { "libc.so", "libc" },
  { "libffi.so", "libffi" },
  { "ld-linux-x86-64.so", "glibc" },
  { "libnss_sss.so", "NSS" },
  { "libsss_debug.so", "SSSD" },
  { "libsss_util.so", "SSSD" },
  { "libnss_systemd.so", "NSS" },
  { "libpcre2-8.so", "PCRE" },
  { "libselinux.so", "SELinux" },
  { "libssl3.so", "NSS" },
  { "libstdc++.so", "libc" },
  { "libsystemd.so", "systemd" },
  { "libudev.so", "udev" },
  { "libxul.so", "XUL" },
  { "libz.so", "Zlib" },
  { "libzstd.so", "Zstd" },

  /* GObject */
  { "libglib-2.0.so", "GLib" },
  { "libgobject-2.0.so", "GObject" },
  { "libgio-2.0.so", "Gio" },
  { "libgirepository-1.0.so", "Introspection" },

  /* Cairo/Pixman */
  { "libcairo-gobject.so", "Cairo" },
  { "libcairo.so", "Cairo" },
  { "libpixman-1.so", "Pixman" },

  /* Javascript */
  { "libgjs.so", "JS" },
  { "libmozjs-102.so", "JS" },
  { "libmozjs-115.so", "JS" },
  { "libjavascriptcoregtk-4.0.so", "JS" },
  { "libjavascriptcoregtk-4.1.so", "JS" },
  { "libjavascriptcoregtk-6.0.so", "JS" },

  /* WebKit */
  { "libwebkit2gtk-4.0.so", "WebKit" },
  { "libwebkit2gtk-4.1.so", "WebKit" },
  { "libwebkitgtk-6.0.so", "WebKit" },

  /* Various GNOME Platform Libraries */
  { "libdex-1.so", "Dex" },
  { "libgstreamer-1-0.so", "GStreamer" },
  { "libgudev-1.0.so", "udev" },
  { "libibus-1.0.so", "IBus" },
  { "libjson-glib-1.0.so", "JSON-GLib" },
  { "libjsonrpc-glib-1.0.so", "JSONRPC-GLib" },
  { "libpolkit-agent-1.so", "PolicyKit" },
  { "libpolkit-gobject-1.so", "PolicyKit" },
  { "libvte-2.91-gtk4.so", "VTE" },
  { "libvte-2.91.so", "VTE" },

  /* Pango and Harfbuzz */
  { "libfribidi.so", "Fribidi" },
  { "libpango-1.0.so", "Pango" },
  { "libpangocairo-1.0.so", "Pango" },
  { "libpangoft2-1.0.so", "Pango" },
  { "libharfbuzz-cairo.so", "Harfbuzz" },
  { "libharfbuzz-gobject.so", "Harfbuzz" },
  { "libharfbuzz-icu.so", "Harfbuzz" },
  { "libharfbuzz-subset.so", "Harfbuzz" },
  { "libharfbuzz.so", "Harfbuzz" },
  { "libfontconfig.so", "FontConfig" },

  /* GTK */
  { "libgtk-3.so", "GTK 3" },
  { "libgdk-3.so", "GTK 3" },
  { "libgtk-4.so", "GTK 4" },
  { "libadwaita-1.so", "Adwaita" },
  { "libgraphene-1.0.so", "Graphene" },
  { "libgdk_pixbuf-2.0.so", "GdkPixbuf" },
  { "librsvg-2.so", "rsvg" },

  /* Xorg/X11 */
  { "libX11-xcb.so", "X11" },
  { "libX11.so", "X11" },
  { "libxcb.so", "X11" },
  { "libxkbcommon.so", "XKB" },

  /* Wayland */
  { "libwayland-client.so", "Wayland Client" },
  { "libwayland-cursor.so", "Wayland Cursor" },
  { "libwayland-egl.so", "Wayland EGL" },
  { "libwayland-server.so", "Wayland Server" },

  /* Mutter/Clutter/Shell */
  { "libclutter-1.0.so", "Clutter" },
  { "libclutter-glx-1.0.so", "Clutter" },
  { "libinput.so", "libinput" },
  { "libmutter-12.so", "Mutter" },
  { "libmutter-13.so", "Mutter" },
  { "libmutter-cogl-12.so", "Mutter" },
  { "libmutter-cogl-13.so", "Mutter" },
  { "libmutter-clutter-12.so", "Mutter" },
  { "libmutter-clutter-13.so", "Mutter" },
  { "libst-12.so", "GNOME Shell" },
  { "libst-13.so", "GNOME Shell" },

  /* GtkSourceView */
  { "libgtksourceview-3.0.so", "GtkSourceView" },
  { "libgtksourceview-4.so", "GtkSourceView" },
  { "libgtksourceview-5.so", "GtkSourceView" },

  /* Pipewire and Legacy Audio modules */
  { "libasound.so", "ALSA" },
  { "libpipewire-0.3.so", "Pipewire" },
  { "libpipewire-module-client-node.so", "Pipewire" },
  { "libpipewire-module-protocol-pulse.so", "Pipewire" },
  { "libpulse.so", "PulseAudio" },
  { "libpulsecommon-16.1.so", "PulseAudio" },
  { "libspa-alsa.so", "Pipewire" },
  { "libspa-audioconvert.so", "Pipewire" },
  { "libspa-support.so", "Pipewire" },

  /* OpenGL base libraries */
  { "libEGL.so", "EGL" },
  { "libGL.so", "GL" },

  /* Mesa and DRI Drivers */
  { "asahi_dri.so", "Mesa" },
  { "apple_dri.so", "Mesa" },
  { "crocus_dri.so", "Mesa" },
  { "i915_dri.so", "Mesa" },
  { "i965_drv_video.so", "Mesa" },
  { "iHD_drv_video.so", "Mesa" },
  { "iris_dri.so", "Mesa" },
  { "kms_swrast_dri.so", "Mesa" },
  { "libEGL_mesa.so", "Mesa" },
  { "libGLX_mesa.so", "Mesa" },
  { "libdrm.so", "Mesa" },
  { "nouveau_dri.so", "Mesa" },
  { "r300_dri.so", "Mesa" },
  { "r600_dri.so", "Mesa" },
  { "radeonsi_dri.so", "Mesa" },
  { "swrast_dri.so", "Mesa" },
  { "virtio_gpu_dri.so", "Mesa" },
  { "vmwgfx_dri.so", "Mesa" },
  { "zink_dri.so", "Mesa" },

  /* Various cryptography libraries */
  { "libcrypto.so", "Cryptography" },
  { "libssl.so", "Cryptography" },
  { "libssl3.so", "Cryptography" },
  { "libgnutls.so", "Cryptography" },
  { "libgnutlsxx.so", "Cryptography" },
  { "libgnutls-dane.so", "Cryptography" },

  /* App Store */
  { "libgnomesoftware.so", "App Store" },
  { "libappstream.so", "App Store" },
  { "libappstream-glib.so", "App Store" },
};

static void
sysprof_elf_finalize (GObject *object)
{
  SysprofElf *self = (SysprofElf *)object;

  g_clear_pointer (&self->file, g_free);
  g_clear_pointer (&self->parser, elf_parser_free);
  g_clear_object (&self->debug_link_elf);

  G_OBJECT_CLASS (sysprof_elf_parent_class)->finalize (object);
}

static void
sysprof_elf_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  SysprofElf *self = SYSPROF_ELF (object);

  switch (prop_id)
    {
    case PROP_BUILD_ID:
      g_value_set_string (value, sysprof_elf_get_build_id (self));
      break;

    case PROP_DEBUG_LINK:
      g_value_set_string (value, sysprof_elf_get_debug_link (self));
      break;

    case PROP_DEBUG_LINK_ELF:
      g_value_set_object (value, sysprof_elf_get_debug_link_elf (self));
      break;

    case PROP_FILE:
      g_value_set_string (value, sysprof_elf_get_file (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  SysprofElf *self = SYSPROF_ELF (object);

  switch (prop_id)
    {
    case PROP_DEBUG_LINK_ELF:
      sysprof_elf_set_debug_link_elf (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_elf_class_init (SysprofElfClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = sysprof_elf_finalize;
  object_class->get_property = sysprof_elf_get_property;
  object_class->set_property = sysprof_elf_set_property;

  properties [PROP_BUILD_ID] =
    g_param_spec_string ("build-id", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DEBUG_LINK] =
    g_param_spec_string ("debug-link", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_DEBUG_LINK_ELF] =
    g_param_spec_object ("debug-link-elf", NULL, NULL,
                         SYSPROF_TYPE_ELF,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE] =
    g_param_spec_string ("file", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  nicks = g_hash_table_new (g_str_hash, g_str_equal);
  for (guint i = 0; i < G_N_ELEMENTS (nick_table); i++)
    {
      g_assert (g_str_has_suffix (nick_table[i].library, ".so"));
      g_hash_table_insert (nicks,
                           (char *)nick_table[i].library,
                           (char *)nick_table[i].nick);
    }
}

static void
sysprof_elf_init (SysprofElf *self)
{
}

static void
guess_nick (SysprofElf *self,
            const char *name,
            const char *endptr)
{
  char key[32];

  if (endptr <= name)
    return;

  if (endptr - name >= sizeof key)
    return;

  memcpy (key, name, endptr-name);
  key[endptr-name] = 0;

  self->nick = g_hash_table_lookup (nicks, key);
}

SysprofElf *
sysprof_elf_new (const char   *filename,
                 GMappedFile  *mapped_file,
                 guint64       file_inode,
                 GError      **error)
{
  SysprofElf *self;
  ElfParser *parser;

  g_return_val_if_fail (mapped_file != NULL, NULL);

  if (!(parser = elf_parser_new_from_mmap (g_steal_pointer (&mapped_file), error)))
    return NULL;

  self = g_object_new (SYSPROF_TYPE_ELF, NULL);
  self->file = g_strdup (filename);
  self->parser = g_steal_pointer (&parser);
  self->file_inode = file_inode;
  self->text_offset = elf_parser_get_text_offset (self->parser);

  if (filename != NULL)
    {
      const char *base;
      const char *endptr;

      if ((base = strrchr (filename, '/')))
        {
          endptr = strstr (++base, ".so");

          if (endptr != NULL && (endptr[3] == 0 || endptr[3] == '.'))
            guess_nick (self, base, &endptr[3]);
        }
    }

  return self;
}

const char *
sysprof_elf_get_file (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return self->file;
}

const char *
sysprof_elf_get_build_id (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return elf_parser_get_build_id (self->parser);
}

const char *
sysprof_elf_get_debug_link (SysprofElf *self)
{
  guint crc32;

  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return elf_parser_get_debug_link (self->parser, &crc32);
}

static char *
sysprof_elf_get_symbol_at_address_internal (SysprofElf *self,
                                            const char *filename,
                                            guint64     address,
                                            guint64    *begin_address,
                                            guint64    *end_address,
                                            guint64     text_offset)
{
  const ElfSym *symbol;
  char *ret = NULL;
  gulong begin = 0;
  gulong end = 0;

  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  if (self->debug_link_elf != NULL)
    {
      ret = sysprof_elf_get_symbol_at_address_internal (self->debug_link_elf, filename, address, begin_address, end_address, text_offset);

      if (ret != NULL)
        return ret;
    }

  if ((symbol = elf_parser_lookup_symbol (self->parser, address - text_offset)))
    {
      const char *name;

      if (begin_address || end_address)
        {
          elf_parser_get_sym_address_range (self->parser, symbol, &begin, &end);
          begin += text_offset;
          end += text_offset;
        }

      name = elf_parser_get_sym_name (self->parser, symbol);

      if (name != NULL && name[0] == '_' && ((name[1] == 'Z') || (name[1] == 'R')))
        ret = elf_demangle (name);
      else
        ret = g_strdup (name);
    }
  else
    {
      return NULL;
    }

  if (begin_address)
    *begin_address = begin;

  if (end_address)
    *end_address = end;

  return ret;
}

char *
sysprof_elf_get_symbol_at_address (SysprofElf *self,
                                   guint64     address,
                                   guint64    *begin_address,
                                   guint64    *end_address)
{
  return sysprof_elf_get_symbol_at_address_internal (self,
                                                     self->file,
                                                     address,
                                                     begin_address,
                                                     end_address,
                                                     self->text_offset);
}

/**
 * sysprof_elf_get_debug_link_elf:
 * @self: a #SysprofElf
 *
 * Gets a #SysprofElf that was resolved from the `.gnu_debuglink`
 * ELF section header.
 *
 * Returns: (transfer none) (nullable): a #SysprofElf or %NULL
 */
SysprofElf *
sysprof_elf_get_debug_link_elf (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return self->debug_link_elf;
}

void
sysprof_elf_set_debug_link_elf (SysprofElf *self,
                                SysprofElf *debug_link_elf)
{
  g_return_if_fail (SYSPROF_IS_ELF (self));
  g_return_if_fail (!debug_link_elf || SYSPROF_IS_ELF (debug_link_elf));
  g_return_if_fail (debug_link_elf != self);

  if (g_set_object (&self->debug_link_elf, debug_link_elf))
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_DEBUG_LINK_ELF]);
}

gboolean
sysprof_elf_matches (SysprofElf *self,
                     guint64     file_inode,
                     const char *build_id)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), FALSE);

  if (build_id != NULL)
    {
      const char *elf_build_id = elf_parser_get_build_id (self->parser);

      /* Not matching build-id, you definitely don't want this ELF */
      if (elf_build_id != NULL && !g_str_equal (build_id, elf_build_id))
        return FALSE;
    }

  if (file_inode && self->file_inode && file_inode != self->file_inode)
    return FALSE;

  return TRUE;
}

const char *
sysprof_elf_get_nick (SysprofElf *self)
{
  g_return_val_if_fail (SYSPROF_IS_ELF (self), NULL);

  return self->nick;
}
