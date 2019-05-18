#include <sysprof-ui.h>

static void
test_zoom_manager (void)
{
  SysprofZoomManager *zoom;

  zoom = sysprof_zoom_manager_new ();
  g_assert_cmpfloat (1.0, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_in (zoom);
  g_assert_cmpfloat (1.5, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_in (zoom);
  g_assert_cmpfloat (2.0, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_in (zoom);
  g_assert_cmpfloat (2.5, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (2.0, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (1.5, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (1.0, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (.9, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (.8, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (.67, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (.5, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (.3, ==, sysprof_zoom_manager_get_zoom (zoom));
  sysprof_zoom_manager_zoom_out (zoom);
  g_assert_cmpfloat (.3 / 2, ==, sysprof_zoom_manager_get_zoom (zoom));

  g_object_unref (zoom);
}


gint
main (gint argc,
      gchar *argv[])
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/ZoomManager/basic", test_zoom_manager);
  return g_test_run ();
}
