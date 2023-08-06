/* sysprof-entry-popover.h
 *
 * Copyright (C) 2015-2022 Christian Hergert <christian@hergert.me>
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
 */

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define SYSPROF_TYPE_ENTRY_POPOVER (sysprof_entry_popover_get_type())

G_DECLARE_DERIVABLE_TYPE (SysprofEntryPopover, sysprof_entry_popover, SYSPROF, ENTRY_POPOVER, GtkPopover)

struct _SysprofEntryPopoverClass
{
  GtkPopoverClass parent;

  /**
   * SysprofEntryPopover::activate:
   * @self: The #SysprofEntryPopover instance.
   * @text: The text at the time of activation.
   *
   * This signal is emitted when the popover's forward button is activated.
   * Connect to this signal to perform your forward progress.
   */
  void (*activate) (SysprofEntryPopover *self,
                    const gchar      *text);

  /**
   * SysprofEntryPopover::insert-text:
   * @self: A #SysprofEntryPopover.
   * @position: the position in UTF-8 characters.
   * @chars: the NULL terminated UTF-8 text to insert.
   * @n_chars: the number of UTF-8 characters in chars.
   *
   * Use this signal to determine if text should be allowed to be inserted
   * into the text buffer. Return GDK_EVENT_STOP to prevent the text from
   * being inserted.
   */
  gboolean (*insert_text) (SysprofEntryPopover *self,
                           guint             position,
                           const gchar      *chars,
                           guint             n_chars);


  /**
   * SysprofEntryPopover::changed:
   * @self: A #SysprofEntryPopover.
   *
   * This signal is emitted when the entry text changes.
   */
  void (*changed) (SysprofEntryPopover *self);
};

GtkWidget   *sysprof_entry_popover_new             (void);
const gchar *sysprof_entry_popover_get_text        (SysprofEntryPopover *self);
void         sysprof_entry_popover_set_text        (SysprofEntryPopover *self,
                                                    const gchar         *text);
const gchar *sysprof_entry_popover_get_message     (SysprofEntryPopover *self);
void         sysprof_entry_popover_set_message     (SysprofEntryPopover *self,
                                                    const gchar         *message);
const gchar *sysprof_entry_popover_get_title       (SysprofEntryPopover *self);
void         sysprof_entry_popover_set_title       (SysprofEntryPopover *self,
                                                    const gchar         *title);
const gchar *sysprof_entry_popover_get_button_text (SysprofEntryPopover *self);
void         sysprof_entry_popover_set_button_text (SysprofEntryPopover *self,
                                                    const gchar         *button_text);
gboolean     sysprof_entry_popover_get_ready       (SysprofEntryPopover *self);
void         sysprof_entry_popover_set_ready       (SysprofEntryPopover *self,
                                                    gboolean             ready);
void         sysprof_entry_popover_select_all      (SysprofEntryPopover *self);

G_END_DECLS
