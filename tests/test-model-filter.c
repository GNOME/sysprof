#include <sysprof-ui.h>
#include <string.h>

#include "util/sp-model-filter.h"

#define TEST_TYPE_ITEM (test_item_get_type())

struct _TestItem
{
  GObject p;
  guint n;
};

G_DECLARE_FINAL_TYPE (TestItem, test_item, TEST, ITEM, GObject)
G_DEFINE_TYPE (TestItem, test_item, G_TYPE_OBJECT)

static void
test_item_class_init (TestItemClass *klass)
{
}

static void
test_item_init (TestItem *self)
{
}

static TestItem *
test_item_new (guint n)
{
  TestItem *item;

  item = g_object_new (TEST_TYPE_ITEM, NULL);
  item->n = n;

  return item;
}

static gboolean
filter_func1 (GObject  *object,
              gpointer  user_data)
{
  return (TEST_ITEM (object)->n & 1) == 0;
}

static gboolean
filter_func2 (GObject  *object,
              gpointer  user_data)
{
  return (TEST_ITEM (object)->n & 1) == 1;
}

static void
test_basic (void)
{
  GListStore *model;
  SpModelFilter *filter;
  TestItem *item;
  guint i;

  model = g_list_store_new (TEST_TYPE_ITEM);
  g_assert (model);

  for (i = 0; i < 1000; i++)
    {
      g_autoptr(TestItem) val = test_item_new (i);

      g_list_store_append (model, val);
    }

  g_assert_cmpint (1000, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));

  filter = sp_model_filter_new (G_LIST_MODEL (model));
  g_assert (filter);

  g_assert_cmpint (1000, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));
  sp_model_filter_set_filter_func (filter, filter_func1, NULL, NULL);
  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  for (i = 0; i < 1000; i += 2)
    g_list_store_remove (model, 998 - i);

  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));
  g_assert_cmpint (0, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  sp_model_filter_set_filter_func (filter, NULL, NULL, NULL);
  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  sp_model_filter_set_filter_func (filter, filter_func2, NULL, NULL);
  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  g_list_store_append (model, test_item_new (1001));

  for (i = 0; i < 500; i++)
    g_list_store_remove (model, 0);

  g_assert_cmpint (1, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));
  g_assert_cmpint (1, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  sp_model_filter_set_filter_func (filter, NULL, NULL, NULL);
  g_assert_cmpint (1, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));
  g_assert_cmpint (1, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  item = g_list_model_get_item (G_LIST_MODEL (filter), 0);
  g_assert (item);
  g_assert (TEST_IS_ITEM (item));
  g_assert_cmpint (item->n, ==, 1001);
  g_clear_object (&item);

  g_clear_object (&model);
  g_clear_object (&filter);
}

static gboolean
filter_keyword_cb (GObject  *object,
                   gpointer  user_data)
{
  SpProcessModelItem *item = SP_PROCESS_MODEL_ITEM (object);
  const gchar *needle = user_data;
  const gchar *haystack = sp_process_model_item_get_command_line (item);

  return NULL != strstr (haystack, needle);
}

static void
test_process (void)
{
  SpProcessModel *model = sp_process_model_new ();
  SpModelFilter *filter;
  static gchar *searches[] = {
    "a", "b", "foo", "bar", "gnome", "gnome-test",
    "gsd", "gsd-", "libexec", "/", ":",
  };

  filter = sp_model_filter_new (G_LIST_MODEL (model));

  sp_process_model_reload (model);

  for (guint i = 0; i < G_N_ELEMENTS (searches); i++)
    {
      sp_model_filter_set_filter_func (filter,
                                       filter_keyword_cb,
                                       g_strdup (searches[i]),
                                       g_free);
      sp_model_filter_invalidate (filter);
      sp_process_model_reload (model);
    }

  g_object_unref (filter);
  g_object_unref (model);
}

gint
main (gint argc,
      gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/SpModelFilter/basic", test_basic);
  g_test_add_func ("/SpModelFilter/process", test_process);
  return g_test_run ();
}
