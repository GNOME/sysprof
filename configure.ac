AC_PREREQ([2.69])


dnl ***********************************************************************
dnl Define Versioning Information
dnl ***********************************************************************
m4_define([major_version],[3])
m4_define([minor_version],[22])
m4_define([micro_version],[3])
m4_define([package_version],[major_version.minor_version.micro_version])
m4_define([bug_report_url],[https://bugzilla.gnome.org/enter_bug.cgi?product=sysprof])
m4_define([api_version],[2])

AX_IS_RELEASE([micro-version])


dnl ***********************************************************************
dnl Initialize autoconf
dnl ***********************************************************************
AC_INIT([sysprof],[package_version],[bug_report_url])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([data/sysprof.pc.in])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_SUBST([ACLOCAL_AMFLAGS], "-I m4")
AC_CANONICAL_HOST


dnl ***********************************************************************
dnl Make version information available to autoconf files
dnl ***********************************************************************
AC_SUBST([MAJOR_VERSION],major_version)
AC_SUBST([MINOR_VERSION],minor_version)
AC_SUBST([MICRO_VERSION],micro_version)
AC_SUBST([API_VERSION],api_version)


dnl ***********************************************************************
dnl Initialize automake
dnl ***********************************************************************
AM_SILENT_RULES([yes])
AM_INIT_AUTOMAKE([1.11 foreign subdir-objects tar-ustar no-dist-gzip dist-xz -Wno-portability])
AM_MAINTAINER_MODE([enable])
AX_GENERATE_CHANGELOG


dnl ***********************************************************************
dnl Add extra debugging with --enable-debug and --enable-compile-warnings
dnl ***********************************************************************
AX_CHECK_ENABLE_DEBUG([no],[]
                      [G_DISABLE_ASSERT G_DISABLE_CHECKS G_DISABLE_CAST_CHECKS])
AX_COMPILER_FLAGS


dnl ***********************************************************************
dnl Internationalization
dnl ***********************************************************************
GETTEXT_PACKAGE=AC_PACKAGE_TARNAME
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [GETTEXT package name])

AM_GNU_GETTEXT_VERSION([0.19.6])
AM_GNU_GETTEXT([external])


dnl ***********************************************************************
dnl Check for required programs
dnl ***********************************************************************
AX_COMPILER_FLAGS_CXXFLAGS
AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AX_CXX_COMPILE_STDCXX_0X
AC_PATH_PROG([GLIB_GENMARSHAL],[glib-genmarshal])
AC_PATH_PROG([GLIB_MKENUMS],[glib-mkenums])
AC_PATH_PROG([GLIB_COMPILE_RESOURCES],[glib-compile-resources])
PKG_PROG_PKG_CONFIG([0.22])
GLIB_GSETTINGS
GOBJECT_INTROSPECTION_CHECK([1.42.0])
VAPIGEN_CHECK
APPSTREAM_XML



dnl ***********************************************************************
dnl Check for recent perf improvements
dnl ***********************************************************************
AC_CHECK_MEMBERS([struct perf_event_attr.use_clockid, struct perf_event_attr.clockid],
                 [AC_DEFINE([HAVE_PERF_CLOCKID],[1],[Define if perf supports clockid])],
                 [],
                 [[#include <linux/perf_event.h>]])


dnl ***********************************************************************
dnl Ensure C11 is Supported
dnl ***********************************************************************
AX_CHECK_COMPILE_FLAG([-std=gnu11],
                      [CFLAGS="$CFLAGS -std=gnu11"],
                      [AC_MSG_ERROR([C compiler cannot compile GNU C11 code])])


dnl ***********************************************************************
dnl GCC 4.8 supports gnu11, but lacked the stdatomic.h headers.
dnl ***********************************************************************
AC_MSG_CHECKING([whether C compiler supports stdatomic.h])
AC_TRY_LINK([#include <stdatomic.h>],
            [atomic_thread_fence(memory_order_acquire);atomic_thread_fence(memory_order_seq_cst);],
	    [AC_MSG_RESULT([yes])],
	    [AC_MSG_ERROR([Sysprof requires a C compiler with stdatomic support such as GCC 4.9 or newer])])


dnl ***********************************************************************
dnl Check for required packages
dnl ***********************************************************************
m4_define([glib_required_version], [2.44.0])
m4_define([gtk_required_version], [3.21.5])
m4_define([systemd_required_version], [222])

PKG_CHECK_MODULES(GIO,
                  [gio-2.0 >= glib_required_version
                   gio-unix-2.0 >= glib_required_version])
PKG_CHECK_MODULES(GTK,
                  [gtk+-3.0 >= gtk_required_version],
                  [have_gtk=yes],
                  [have_gtk=no])
PKG_CHECK_MODULES(POLKIT,
                  [polkit-gobject-1],
                  [have_polkit=yes],
                  [have_polkit=no])
PKG_CHECK_MODULES(SYSTEMD,
                  [libsystemd >= systemd_required_version],
                  [have_systemd=yes],
                  [have_systemd=no])
LT_LIB_M

# sysprofd requires libsystemd and polkit, and is required to run
# the ui without root permissions
AC_ARG_WITH([sysprofd],
             [AS_HELP_STRING([--with-sysprofd=@<:@host/bundled/no/auto@:>@],
                              [Build Sysprof D-Bus daemon.])],
             [with_sysprofd=$withval],
             [with_sysprofd=auto])
AS_IF([test "$with_sysprofd" = "auto"],[
        AS_IF([test "$have_polkit" = "yes" && test "$have_systemd" = "yes"],
              [with_sysprofd=bundled],
              [with_sysprofd=no])
])
AS_IF([test "$with_sysprofd" = "bundled"],[
        AS_IF([test "$have_systemd" = "yes" && test "$have_polkit" = "yes"],[],[
                AC_MSG_ERROR([--enable-sysprofd requires libsystemd >= systemd_required_version and polkit-gobject-1])
        ])
])
AM_CONDITIONAL(ENABLE_SYSPROFD, [test "$with_sysprofd" = "bundled"])
AM_CONDITIONAL(ENABLE_POLKIT, [test "$with_sysprofd" = "host" || test "$with_sysprofd" = "bundled"])

# we might be building sysprof-cli without the UI
AC_ARG_ENABLE([gtk],
              [AS_HELP_STRING([--enable-gtk=@<:@yes/no/auto@:>@],
                              [Build GTK user interface.])],
              [enable_gtk=$enableval],
              [enable_gtk=auto])
AS_IF([test "$enable_gtk" = auto],[
	AS_IF([test "$have_gtk" = "yes" && test "$have_polkit" = "yes"],[enable_gtk=yes],[enable_gtk=no])
])
AS_IF([test "$enable_gtk" = "yes"],[
	AS_IF([test "$have_gtk" = "yes" && test "$have_polkit" = "yes"],[],[
		AC_MSG_ERROR([--enable-gtk requires gtk+-3.0 >= gtk_required_version and polkit-gobject-1])
	])
])
AM_CONDITIONAL(ENABLE_GTK, test "$enable_gtk" = "yes")


# where to place systemd units if necessary
AC_ARG_WITH([systemdsystemunitdir],
            AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
                           [Directory for systemd service files]),
            [],
            [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != "xno"; then
  AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi


dnl ***********************************************************************
dnl Override location of debug symbols for system libraries
dnl ***********************************************************************
debugdir=${libdir}/debug

# Separate debug dir
	 
dnl written by Guido Draheim <guidod@gmx.de>, original by Alexandre Oliva 
dnl Version 1.3 (2001/03/02)
dnl source http://www.gnu.org/software/ac-archive/Miscellaneous/ac_define_dir.html

AC_DEFUN([AC_DEFINE_DIR], [
  test "x$prefix" = xNONE && prefix="$ac_default_prefix"
  test "x$exec_prefix" = xNONE && exec_prefix='${prefix}'
  ac_define_dir=`eval echo [$]$2`
  ac_define_dir=`eval echo [$]ac_define_dir`
  ifelse($3, ,
    AC_DEFINE_UNQUOTED($1, "$ac_define_dir"),
    AC_DEFINE_UNQUOTED($1, "$ac_define_dir", $3))
])

AC_ARG_WITH(separate-debug-dir,
            [  --with-separate-debug-dir=path   Look for global separate debug info in this path [LIBDIR/debug]],
            [debugdir="${withval}"])
	
AC_DEFINE_DIR(DEBUGDIR, debugdir,
              [Look for global separate debug info in this path])

AC_SUBST([LIBEXECDIR], `eval echo "${libexecdir}"`)


dnl ***********************************************************************
dnl User documentation
dnl ***********************************************************************
YELP_HELP_INIT


dnl ***********************************************************************
dnl Initialize Libtool
dnl ***********************************************************************
LT_PREREQ([2.2])
LT_INIT


dnl ***********************************************************************
dnl Process .in Files
dnl ***********************************************************************
AC_CONFIG_FILES([
	Makefile

	lib/Makefile
	lib/sysprof-version.h

	daemon/Makefile

	data/Makefile
	data/icons/Makefile
	data/sysprof-$API_VERSION.pc:data/sysprof.pc.in
	data/sysprof-ui-$API_VERSION.pc:data/sysprof-ui.pc.in
	data/org.gnome.Sysprof2.conf
	data/org.gnome.sysprof2.policy

	help/Makefile

	po/Makefile.in

	src/Makefile

	tests/Makefile

	tools/Makefile
],[],
[API_VERSION='$API_VERSION'])
AC_OUTPUT


echo ""
echo " ${PACKAGE} - ${VERSION}"
echo ""
echo " Options"
echo ""
echo "  Prefix ....................................... : ${prefix}"
echo "  Libdir ....................................... : ${libdir}"
echo "  Libexecdir ................................... : ${libexecdir}"
echo ""
echo "  Debug Symbols Directory ...................... : ${debugdir}"
echo ""
echo "    The sysprof interface will look in this"
echo "    directory to load symbols for libraries that"
echo "    have symbols stripped."
echo ""
echo "  sysprof-cli .................................. : yes"
echo ""
echo "    The sysprof-cli command line tool can be used"
echo "    to profile systems where there is no access"
echo "    to GTK. It also has less overhead than the"
echo "    full GTK interface."
echo ""
echo "  sysprof GTK Interface  ....................... : ${enable_gtk}"
echo ""
echo "    The GTK interface can explore sysprof capture"
echo "    files. If you want the user interface, ensure"
echo "    this is yes."
echo ""
echo "  sysprofd ..................................... : ${with_sysprofd}"
echo ""
echo "    The sysprofd system daemon provides support"
echo "    for accessing hardware performance counters"
echo "    as a normal user."
echo ""
echo "    ** Without this, whole-system capture will **"
echo "    ** require running as root.                **"
echo ""
echo "  Systemd System Units ......................... : ${with_systemdsystemunitdir}"
echo ""
echo "    sysprofd uses systemd to manage the service"
echo "    and sd-bus for D-Bus communication."
echo ""
echo "    ** Make sure this is somewhere that systemd **"
echo "    ** can locate your service files, or the UI **"
echo "    ** will not be able to elevate privileges.  **"
echo ""
