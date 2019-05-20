#include <sysprof-ui.h>
#include <string.h>

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
  SysprofModelFilter *filter;
  TestItem *item;
  guint i;

  model = g_list_store_new (TEST_TYPE_ITEM);
  g_assert (model);

  filter = sysprof_model_filter_new (G_LIST_MODEL (model));
  g_assert (filter);

  for (i = 0; i < 1000; i++)
    {
      g_autoptr(TestItem) val = test_item_new (i);

      g_list_store_append (model, val);
    }

  g_assert_cmpint (1000, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));
  g_assert_cmpint (1000, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  g_assert_cmpint (1000, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));
  sysprof_model_filter_set_filter_func (filter, filter_func1, NULL, NULL);
  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  for (i = 0; i < 500; i++)
    {
      g_autoptr(TestItem) ele = g_list_model_get_item (G_LIST_MODEL (filter), i);

      g_assert (TEST_IS_ITEM (ele));
      g_assert (filter_func1 (G_OBJECT (ele), NULL));
    }

  for (i = 0; i < 1000; i += 2)
    g_list_store_remove (model, 998 - i);

  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));
  g_assert_cmpint (0, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  sysprof_model_filter_set_filter_func (filter, NULL, NULL, NULL);
  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  sysprof_model_filter_set_filter_func (filter, filter_func2, NULL, NULL);
  g_assert_cmpint (500, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  {
    g_autoptr(TestItem) freeme = test_item_new (1001);
    g_list_store_append (model, freeme);
  }

  for (i = 0; i < 500; i++)
    g_list_store_remove (model, 0);

  g_assert_cmpint (1, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));
  g_assert_cmpint (1, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  sysprof_model_filter_set_filter_func (filter, NULL, NULL, NULL);
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
  SysprofProcessModelItem *item = SYSPROF_PROCESS_MODEL_ITEM (object);
  const gchar *haystack = sysprof_process_model_item_get_command_line (item);
  const gchar *needle = user_data;

  if (haystack && needle)
    return strstr (haystack, needle) != NULL;

  return FALSE;
}

static void
test_process (void)
{
  SysprofProcessModel *model = sysprof_process_model_new ();
  SysprofModelFilter *filter;
  static gchar *searches[] = {
    "a", "b", "foo", "bar", "gnome", "gnome-test",
    "libexec", "/", ":", "gsd-",
  };

  filter = sysprof_model_filter_new (G_LIST_MODEL (model));

  sysprof_process_model_set_no_proxy (model, TRUE);
  sysprof_process_model_reload (model);

  for (guint i = 0; i < G_N_ELEMENTS (searches); i++)
    {
      const gchar *needle = searches[i];
      guint n_items;

      sysprof_model_filter_set_filter_func (filter, filter_keyword_cb, g_strdup (needle), g_free);

      n_items = g_list_model_get_n_items (G_LIST_MODEL (filter));

      for (guint j = 0; j < n_items; j++)
        {
          g_autoptr(SysprofProcessModelItem) item = g_list_model_get_item (G_LIST_MODEL (filter), j);

          g_assert (SYSPROF_IS_PROCESS_MODEL_ITEM (item));
          g_assert (filter_keyword_cb (G_OBJECT (item), (gchar *)needle));

          //g_print ("%s: %s\n", needle, sysprof_process_model_item_get_command_line (item));
        }
    }

  g_object_unref (filter);
  g_object_unref (model);
}

static guint last_n_added = 0;
static guint last_n_removed = 0;
static guint last_changed_position = 0;

static void
model_items_changed_cb (SysprofModelFilter *filter,
                        guint          position,
                        guint          n_removed,
                        guint          n_added,
                        GListModel    *model)
{
  last_n_added = n_added;
  last_n_removed = n_removed;
  last_changed_position = position;
}


static void
filter_items_changed_cb (SysprofModelFilter *filter,
                         guint          position,
                         guint          n_removed,
                         guint          n_added,
                         GListModel    *model)
{
  g_assert_cmpint (n_added, ==, last_n_added);
  g_assert_cmpint (n_removed, ==, last_n_removed);
  g_assert_cmpint (position, ==, last_changed_position);
}

static void
test_items_changed (void)
{
  SysprofModelFilter *filter;
  GListStore *model;
  guint i;

  model = g_list_store_new (TEST_TYPE_ITEM);
  g_assert (model);

  g_signal_connect (model, "items-changed", G_CALLBACK (model_items_changed_cb), NULL);

  filter = sysprof_model_filter_new (G_LIST_MODEL (model));
  g_assert (filter);

  g_signal_connect_after (filter, "items-changed", G_CALLBACK (filter_items_changed_cb), model);

  /* The filter model is not filtered, so it must mirror whatever
   * the child model does. In this case, test if the position of
   * items-changed match.
   */
  for (i = 0; i < 100; i++)
    {
      g_autoptr (TestItem) val = test_item_new (i);
      g_list_store_append (model, val);
    }

  g_assert_cmpint (100, ==, g_list_model_get_n_items (G_LIST_MODEL (model)));
  g_assert_cmpint (100, ==, g_list_model_get_n_items (G_LIST_MODEL (filter)));

  for (i = 0; i < 100; i++)
    g_list_store_remove (model, 0);

  g_clear_object (&model);
  g_clear_object (&filter);
}

gint
main (gint argc,
      gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/SysprofModelFilter/basic", test_basic);
  g_test_add_func ("/SysprofModelFilter/process", test_process);
  g_test_add_func ("/SysprofModelFilter/items-changed", test_items_changed);
  return g_test_run ();
}
