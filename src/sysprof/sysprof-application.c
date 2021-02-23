/* sysprof-application.c
 *
 * Copyright 2016 Christian Hergert <chergert@redhat.com>
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

#include "sysprof-application.h"
#include "sysprof-credits.h"
#include "sysprof-window.h"

struct _SysprofApplication
{
  DzlApplication parent_instance;
};

G_DEFINE_TYPE (SysprofApplication, sysprof_application, DZL_TYPE_APPLICATION)

struct {
  const gchar *action_name;
  const gchar *accels[12];
} default_accels[] = {
  { "app.help",           { "F1", NULL } },
  { "app.new-window",     { "<Primary>n", NULL } },
  { "app.open-capture",   { "<Primary>o", NULL } },
  { "zoom.zoom-in",       { "<Primary>plus", "<Primary>KP_Add", "<Primary>equal", "ZoomIn", NULL } },
  { "zoom.zoom-out",      { "<Primary>minus", "<Primary>KP_Subtract", "ZoomOut", NULL } },
  { "zoom.zoom-one",      { "<Primary>0", "<Primary>KP_0", NULL } },
  { "win.new-tab",        { "<Primary>t", NULL } },
  { "win.close-tab",      { "<Primary>w", NULL } },
  { "win.replay-capture", { "<Primary>r", NULL } },
  { "win.save-capture",   { "<Primary>s", NULL } },
  { "win.switch-tab(1)",  { "<Alt>1", NULL } },
  { "win.switch-tab(2)",  { "<Alt>2", NULL } },
  { "win.switch-tab(3)",  { "<Alt>3", NULL } },
  { "win.switch-tab(4)",  { "<Alt>4", NULL } },
  { "win.switch-tab(5)",  { "<Alt>5", NULL } },
  { "win.switch-tab(6)",  { "<Alt>6", NULL } },
  { "win.switch-tab(7)",  { "<Alt>7", NULL } },
  { "win.switch-tab(8)",  { "<Alt>8", NULL } },
  { "win.switch-tab(9)",  { "<Alt>9", NULL } },
  { NULL }
};

static void
sysprof_application_activate (GApplication *app)
{
  SysprofWindow *window;
  GList *windows;

  g_assert (GTK_IS_APPLICATION (app));

  windows = gtk_application_get_windows (GTK_APPLICATION (app));

  for (; windows != NULL; windows = windows->next)
    {
      if (SYSPROF_IS_WINDOW (windows->data))
        {
          gtk_window_present (windows->data);
          return;
        }
    }

  window = SYSPROF_WINDOW (sysprof_window_new (SYSPROF_APPLICATION (app)));
  gtk_window_present (GTK_WINDOW (window));
}

static void
sysprof_application_open (GApplication  *app,
                          GFile        **files,
                          gint           n_files,
                          const gchar   *hint)
{
  GtkWidget *window;

  g_assert (SYSPROF_IS_APPLICATION (app));
  g_assert (files != NULL || n_files == 0);

  window = sysprof_window_new (SYSPROF_APPLICATION (app));

  /* Present window before opening files so that message dialogs
   * always display above the window.
   */
  gtk_window_present (GTK_WINDOW (window));

  for (gint i = 0; i < n_files; i++)
    sysprof_window_open (SYSPROF_WINDOW (window), files[i]);

  if (n_files == 0)
    sysprof_application_activate (app);
}

static void
sysprof_application_startup (GApplication *application)
{
  g_autoptr(GtkCssProvider) provider = NULL;
#ifdef DEVELOPMENT_BUILD
  g_autoptr(GtkCssProvider) adwaita = NULL;
#endif

  g_assert (SYSPROF_IS_APPLICATION (application));

  G_APPLICATION_CLASS (sysprof_application_parent_class)->startup (application);

  provider = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (provider, "/org/gnome/sysprof/theme/shared.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (provider),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

#ifdef DEVELOPMENT_BUILD
  adwaita = gtk_css_provider_new ();
  gtk_css_provider_load_from_resource (adwaita, "/org/gnome/sysprof/theme/Adwaita-shared.css");
  gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                             GTK_STYLE_PROVIDER (adwaita),
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
#endif

  for (guint i = 0; default_accels[i].action_name; i++)
    gtk_application_set_accels_for_action (GTK_APPLICATION (application),
                                           default_accels[i].action_name,
                                           default_accels[i].accels);
}

static void
sysprof_application_class_init (SysprofApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  app_class->open = sysprof_application_open;
  app_class->startup = sysprof_application_startup;
  app_class->activate = sysprof_application_activate;
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
      if (SYSPROF_IS_WINDOW (windows->data))
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
                         "copyright", "Copyright 2004-2009 Søren Sandmann Pedersen\n"
                                      "Copyright 2016-2021 Christian Hergert",
                         "transient-for", best_toplevel,
                         "modal", TRUE,
                         "translator-credits", _("translator-credits"),
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "logo-icon-name", "org.gnome.Sysprof",
                         "program-name", _("Sysprof"),
                         "version", SYMBOLIC_VERSION "\n" "(" PACKAGE_VERSION ")",
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
  SysprofApplication *self = user_data;
  GtkWindow *window;

  g_assert (SYSPROF_IS_APPLICATION (self));
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
  SysprofApplication *self = user_data;
  SysprofWindow *window;

  g_assert (SYSPROF_IS_APPLICATION (self));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  window = SYSPROF_WINDOW (sysprof_window_new (self));
  gtk_window_present (GTK_WINDOW (window));
}

static void
sysprof_open_capture (GSimpleAction *action,
                      GVariant      *variant,
                      gpointer       user_data)
{
  GtkApplication *app = user_data;
  GList *list;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  list = gtk_application_get_windows (app);

  for (; list != NULL; list = list->next)
    {
      GtkWindow *window = list->data;

      if (SYSPROF_IS_WINDOW (window))
        {
          sysprof_window_open_from_dialog (SYSPROF_WINDOW (window));
          break;
        }
    }
}

static void
sysprof_show_help_overlay (GSimpleAction *action,
                           GVariant      *variant,
                           gpointer       user_data)
{
  GtkApplication *app = user_data;
  SysprofWindow *window = NULL;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  for (GList *list = gtk_application_get_windows (app); list; list = list->next)
    {
      GtkWindow *element = list->data;

      if (SYSPROF_IS_WINDOW (element))
        {
          window = SYSPROF_WINDOW (element);
          break;
        }
    }

  if (window == NULL)
    {
      window = SYSPROF_WINDOW (sysprof_window_new (SYSPROF_APPLICATION (app)));
      gtk_window_present (GTK_WINDOW (window));
    }

  g_assert (g_action_group_has_action (G_ACTION_GROUP (window), "show-help-overlay"));

  g_action_group_activate_action (G_ACTION_GROUP (window), "show-help-overlay", NULL);
}

static void
sysprof_application_init (SysprofApplication *self)
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

SysprofApplication *
sysprof_application_new (void)
{
  return g_object_new (SYSPROF_TYPE_APPLICATION,
                       "application-id", "org.gnome.Sysprof3",
                       "resource-base-path", "/org/gnome/sysprof",
                       "flags", G_APPLICATION_HANDLES_OPEN,
                       NULL);
}
