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
#include "sysprof-greeter.h"
#include "sysprof-recording-template.h"
#include "sysprof-window.h"

struct _SysprofApplication
{
  AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (SysprofApplication, sysprof_application, ADW_TYPE_APPLICATION)

static const GOptionEntry option_entries[] = {
  { "greeter", 'g', 0, G_OPTION_ARG_NONE, NULL, N_("Start a new recording") },
  { "version", 0, 0, G_OPTION_ARG_NONE, NULL, N_("Show Sysprof version and exit") },
  { 0 }
};

enum {
  PROP_0,
  PROP_DESCENDANT_MENU,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
sysprof_application_activate (GApplication *app)
{
  GtkWindow *window;
  GtkWidget *greeter;

  g_assert (GTK_IS_APPLICATION (app));

  window = gtk_application_get_active_window (GTK_APPLICATION (app));

  if (window)
    {
      gtk_window_present (window);
      return;
    }

  greeter = sysprof_greeter_new ();
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (greeter));
  gtk_window_present (GTK_WINDOW (greeter));
}

static void
sysprof_application_open (GApplication  *app,
                          GFile        **files,
                          int            n_files,
                          const char    *hint)
{
  g_autoptr(SysprofRecordingTemplate) template = NULL;

  g_assert (SYSPROF_IS_APPLICATION (app));
  g_assert (files != NULL || n_files == 0);

  template = sysprof_recording_template_new_from_file (NULL, NULL);

  for (guint i = 0; i < n_files; i++)
    sysprof_window_open (SYSPROF_APPLICATION (app), template, files[i]);
}

static int
sysprof_application_command_line (GApplication            *app,
                                  GApplicationCommandLine *command_line)
{
  g_auto(GStrv) argv = NULL;
  g_autoptr(GPtrArray) files = NULL;
  GVariantDict *options;
  int argc;
  gboolean greeter = FALSE;

  g_assert (SYSPROF_IS_APPLICATION (app));
  g_assert (G_IS_APPLICATION_COMMAND_LINE (command_line));

  argv = g_application_command_line_get_arguments (command_line, &argc);
  options = g_application_command_line_get_options_dict (command_line);
  files = g_ptr_array_new_with_free_func (g_object_unref);

  for (int i = 1; i < argc; i++)
    {
      const char *filename = argv[i];
      g_autoptr(GFile) file = NULL;
      file = g_application_command_line_create_file_for_arg (command_line, filename);
      g_ptr_array_add (files, g_steal_pointer (&file));
    }

  if (files->len > 0)
    g_application_open (app,
                        (GFile **)(gpointer)files->pdata,
                        files->len,
                        NULL);

  if (g_variant_dict_lookup (options, "greeter", "b", &greeter) && greeter)
    g_action_group_activate_action (G_ACTION_GROUP (app), "new-window", NULL);

  if (files->len == 0 && !greeter)
    g_application_activate (app);

  return EXIT_SUCCESS;
}

static int
sysprof_application_handle_local_options (GApplication *app,
                                          GVariantDict *options)
{
  gboolean version = FALSE;

  g_assert (SYSPROF_IS_APPLICATION (app));

  if (g_variant_dict_lookup (options, "version", "b", &version) && version)
    {
      g_print ("Sysprof version " PACKAGE_VERSION "\n");
      return 0;
    }

  return G_APPLICATION_CLASS (sysprof_application_parent_class)->handle_local_options (app, options);
}

static void
sysprof_application_window_added (GtkApplication *application,
                                  GtkWindow      *window)
{
  g_assert (SYSPROF_IS_APPLICATION (application));
  g_assert (GTK_IS_WINDOW (window));

#if DEVELOPMENT_BUILD
  gtk_widget_add_css_class (GTK_WIDGET (window), "devel");
#endif

  GTK_APPLICATION_CLASS (sysprof_application_parent_class)->window_added (application, window);
}

static void
sysprof_application_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  SysprofApplication *self = SYSPROF_APPLICATION (object);

  switch (prop_id)
    {
    case PROP_DESCENDANT_MENU:
      g_value_set_object (value, gtk_application_get_menu_by_id (GTK_APPLICATION (self), "descendant_menu"));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
sysprof_application_class_init (SysprofApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);
  GtkApplicationClass *gtk_app_class = GTK_APPLICATION_CLASS (klass);

  object_class->get_property = sysprof_application_get_property;

  app_class->open = sysprof_application_open;
  app_class->activate = sysprof_application_activate;
  app_class->command_line = sysprof_application_command_line;
  app_class->handle_local_options = sysprof_application_handle_local_options;

  gtk_app_class->window_added = sysprof_application_window_added;

  properties[PROP_DESCENDANT_MENU] =
    g_param_spec_object ("descendant-menu", NULL, NULL,
                         G_TYPE_MENU_MODEL,
                         (G_PARAM_READABLE|
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
sysprof_new_window (GSimpleAction *action,
                    GVariant      *variant,
                    gpointer       user_data)
{
  const GList *windows;
  GApplication *app = user_data;
  GtkWidget *greeter;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  windows = gtk_application_get_windows (GTK_APPLICATION (app));

  for (const GList *iter = windows; iter != NULL; iter = iter->next)
    {
      if (SYSPROF_IS_GREETER (iter->data))
        {
          gtk_window_present (iter->data);
          return;
        }
    }

  greeter = sysprof_greeter_new ();
  gtk_application_add_window (GTK_APPLICATION (app), GTK_WINDOW (greeter));
  gtk_window_present (GTK_WINDOW (greeter));
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
  GtkWindow *window = NULL;

  g_assert (G_IS_APPLICATION (app));
  g_assert (G_IS_SIMPLE_ACTION (action));
  g_assert (variant == NULL);

  window = gtk_application_get_active_window (GTK_APPLICATION (app));

  if (window == NULL)
    {
      g_warning ("No windows found to show about dialog");
      return;
    }

  adw_show_about_dialog (GTK_WIDGET (window),
                         "application-name", _("Sysprof"),
                         "application-icon", APP_ID_S,
                         "version", PACKAGE_VERSION,
                         "copyright", "Copyright 2004-2009 Søren Sandmann Pedersen\n"
                                      "Copyright 2016-2023 Christian Hergert",
                         "issue-url", "https://gitlab.gnome.org/GNOME/sysprof/-/issues/new",
                         "license-type", GTK_LICENSE_GPL_3_0,
                         "developers", sysprof_authors,
                         "artists", sysprof_artists,
                         "comments", _("A system profiler"),
                         "translator-credits", _("translator-credits"),
                         "website", "https://apps.gnome.org/Sysprof/",
                         NULL);
}

static void
sysprof_help (GSimpleAction *action,
              GVariant      *param,
              gpointer       user_data)
{
  SysprofApplication *self = user_data;
  g_autoptr(GtkUriLauncher) launcher = NULL;
  GtkWindow *window;

  g_assert (SYSPROF_IS_APPLICATION (self));
  g_assert (G_IS_SIMPLE_ACTION (action));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  launcher = gtk_uri_launcher_new ("help:sysprof");
  gtk_uri_launcher_launch (launcher, window, NULL, NULL, NULL);
}

static void
sysprof_show_help_overlay (GSimpleAction *action,
                           GVariant      *variant,
                           gpointer       user_data)
{
  SysprofApplication *self = user_data;
  g_autoptr(GtkBuilder) builder = NULL;
  GtkWindow *window;
  GObject *help_overlay;

  g_assert (SYSPROF_IS_APPLICATION (self));
  g_assert (G_IS_SIMPLE_ACTION (action));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));
  builder = gtk_builder_new_from_resource ("/org/gnome/sysprof/gtk/help-overlay.ui");
  help_overlay = gtk_builder_get_object (builder, "help_overlay");

  if (GTK_IS_SHORTCUTS_WINDOW (help_overlay))
    {
#if DEVELOPMENT_BUILD
      gtk_widget_add_css_class (GTK_WIDGET (help_overlay), "devel");
#endif
      gtk_window_set_transient_for (GTK_WINDOW (help_overlay), GTK_WINDOW (window));
      gtk_window_present (GTK_WINDOW (help_overlay));
    }

}

static void
sysprof_application_init (SysprofApplication *self)
{
  static const GActionEntry actions[] = {
    { "new-window", sysprof_new_window },
    { "about", sysprof_about },
    { "show-help-overlay", sysprof_show_help_overlay },
    { "help",  sysprof_help },
    { "quit",  sysprof_quit },
  };

  setlocale (LC_ALL, "");

  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  g_set_application_name (_("Sysprof"));
  gtk_window_set_default_icon_name (APP_ID_S);

  g_application_add_main_option_entries (G_APPLICATION (self), option_entries);

  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   actions,
                                   G_N_ELEMENTS (actions),
                                   self);

  g_application_set_default (G_APPLICATION (self));
}

SysprofApplication *
sysprof_application_new (void)
{
  return g_object_new (SYSPROF_TYPE_APPLICATION,
                       "application-id", APP_ID_S,
                       "resource-base-path", "/org/gnome/sysprof",
                       "flags", G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_HANDLES_OPEN,
                       NULL);
}
