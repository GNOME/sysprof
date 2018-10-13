/* sp-application.c
 *
 * Copyright (C) 2016 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include <glib/gi18n.h>
#include <locale.h>

#include "sp-application.h"
#include "sp-credits.h"
#include "sp-window.h"

struct _SpApplication
{
  GtkApplication parent_instance;
};

G_DEFINE_TYPE (SpApplication, sp_application, GTK_TYPE_APPLICATION)

struct {
  const gchar *action_name;
  const gchar *accels[12];
} default_accels[] = {
  { "zoom.zoom-in", { "<Primary>plus", "<Primary>KP_Add", "<Primary>equal", "ZoomIn", NULL } },
  { "zoom.zoom-out", { "<Primary>minus", "<Primary>KP_Subtract", "ZoomOut", NULL } },
  { "zoom.zoom-one", { "<Primary>0", "<Primary>KP_0", NULL } },
  { NULL }
};

static void
sp_application_activate (GApplication *app)
{
  SpWindow *window;
  GList *windows;

  g_assert (GTK_IS_APPLICATION (app));

  windows = gtk_application_get_windows (GTK_APPLICATION (app));

  for (; windows != NULL; windows = windows->next)
    {
      if (SP_IS_WINDOW (windows->data))
        {
          gtk_window_present (windows->data);
          return;
        }
    }

  window = g_object_new (SP_TYPE_WINDOW,
                         "application", app,
                         NULL);

  gtk_window_present (GTK_WINDOW (window));
}

static void
sp_application_open (GApplication  *app,
                     GFile        **files,
                     gint           n_files,
                     const gchar   *hint)
{
  guint opened = 0;
  gint i;

  g_assert (SP_IS_APPLICATION (app));
  g_assert (files != NULL || n_files == 0);

  for (i = 0; i < n_files; i++)
    {
      SpWindow *window;

      window = g_object_new (SP_TYPE_WINDOW,
                             "application", app,
                             NULL);
      sp_window_open (window, files [i]);
      gtk_window_present (GTK_WINDOW (window));
      opened++;
    }

  if (opened == 0)
    sp_application_activate (app);
}

static void
sp_application_startup (GApplication *application)
{
  g_autoptr(GtkCssProvider) provider = NULL;

  g_assert (SP_IS_APPLICATION (application));

  G_APPLICATION_CLASS (sp_application_parent_class)->startup (application);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/sysprof/theme/shared.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  for (guint i = 0; default_accels[i].action_name; i++)
    gtk_application_set_accels_for_action (GTK_APPLICATION (application),
                                           default_accels[i].action_name,
                                           default_accels[i].accels);
}

static void
sp_application_class_init (SpApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  app_class->open = sp_application_open;
  app_class->startup = sp_application_startup;
  app_class->activate = sp_application_activate;
}

static void
sysprof_quit (GSimpleAction *action,
              GVariant      *variant,
              gpointer       user_data)
{
  GApplication *app = user_data;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  g_application_quit (app);
}

static void
sysprof_about (GSimpleAction *action,
               GVariant      *variant,
               gpointer       user_data)
{
  GtkApplication *app = user_data;
  GtkWindow *best_toplevel = NULL;
  GtkWindow *dialog;
  GList *windows;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  windows = gtk_application_get_windows (app);

  for (; windows != NULL; windows = windows->next)
    {
      if (SP_IS_WINDOW (windows->data))
        {
          best_toplevel = windows->data;
          break;
        }
    }

  dialog = g_object_new (GTK_TYPE_ABOUT_DIALOG,
                         "application", app,
                         "authors", sysprof_authors,
                         "artists", sysprof_artists,
                         "comments", _("A system profiler"),
                         "copyright", "Copyright © 2004-2009 Søren Sandmann Pedersen\n"
                                      "Copyright © 2016-2018 Christian Hergert",
                         "transient-for", best_toplevel,
                         "modal", TRUE,
                         "translator-credits", _("translator-credits"),
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "logo-icon-name", "sysprof",
                         "program-name", _("Sysprof"),
                         "version", PACKAGE_VERSION,
                         "website", "https://wiki.gnome.org/Apps/Sysprof",
                         "website-label", _("Learn more about Sysprof"),
                         NULL);

  g_signal_connect (dialog,
                    "response",
                    G_CALLBACK (gtk_widget_destroy),
                    NULL);

  gtk_window_present (dialog);
}

static void
sysprof_help (GSimpleAction *action,
              GVariant      *param,
              gpointer       user_data)
{
  SpApplication *self = user_data;
  GtkWindow *window;

  g_assert (SP_IS_APPLICATION (self));
  g_assert (G_IS_SIMPLE_ACTION (action));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  gtk_show_uri_on_window (window,
                          "help:sysprof",
                          gtk_get_current_event_time (),
                          NULL);
}

static void
sysprof_new_window (GSimpleAction *action,
                    GVariant      *variant,
                    gpointer       user_data)
{
  SpApplication *self = user_data;
  SpWindow *window;

  g_assert (SP_IS_APPLICATION (self));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  window = g_object_new (SP_TYPE_WINDOW,
                         "application", self,
                         NULL);
  gtk_window_present (GTK_WINDOW (window));
}

static void
sysprof_open_capture (GSimpleAction *action,
                      GVariant      *variant,
                      gpointer       user_data)
{
  GtkApplication *app = user_data;
  GtkWidget *window;
  GList *list;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  list = gtk_application_get_windows (app);

  for (; list != NULL; list = list->next)
    {
      window = list->data;

      if (SP_IS_WINDOW (window))
        {
          SpWindowState state;

          state = sp_window_get_state (SP_WINDOW (window));

          if (state == SP_WINDOW_STATE_EMPTY)
            {
              sp_window_open_from_dialog (SP_WINDOW (window));
              return;
            }
        }
    }

  window = g_object_new (SP_TYPE_WINDOW,
                         "application", app,
                         NULL);

  gtk_window_present (GTK_WINDOW (window));

  sp_window_open_from_dialog (SP_WINDOW (window));
}

static void
sysprof_show_help_overlay (GSimpleAction *action,
                           GVariant      *variant,
                           gpointer       user_data)
{
  GtkApplication *app = user_data;
  SpWindow *window = NULL;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  for (GList *list = gtk_application_get_windows (app); list; list = list->next)
    {
      GtkWindow *element = list->data;

      if (SP_IS_WINDOW (element))
        {
          window = SP_WINDOW (element);
          break;
        }
    }

  if (window == NULL)
    {
      window = g_object_new (SP_TYPE_WINDOW,
                             "application", app,
                             NULL);
      gtk_window_present (GTK_WINDOW (window));
    }

  g_assert (g_action_group_has_action (G_ACTION_GROUP (window), "show-help-overlay"));

  g_action_group_activate_action (G_ACTION_GROUP (window), "show-help-overlay", NULL);
}

static void
sp_application_init (SpApplication *self)
{
  static const GActionEntry actions[] = {
    { "about",             sysprof_about },
    { "new-window",        sysprof_new_window },
    { "open-capture",      sysprof_open_capture },
    { "help",              sysprof_help },
    { "show-help-overlay", sysprof_show_help_overlay },
    { "quit",              sysprof_quit },
  };

  setlocale (LC_ALL, "");

  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_set_application_name (_("Sysprof"));

  g_action_map_add_action_entries (G_ACTION_MAP (self), actions, G_N_ELEMENTS (actions), self);

  g_application_set_default (G_APPLICATION (self));
}

SpApplication *
sp_application_new (void)
{
  return g_object_new (SP_TYPE_APPLICATION,
                       "application-id", "org.gnome.Sysprof2",
                       "resource-base-path", "/org/gnome/sysprof",
                       "flags", G_APPLICATION_HANDLES_OPEN,
                       NULL);
}
