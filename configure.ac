AC_PREREQ(2.54)

AC_INIT([sysprof], [1.1.6])
AC_CONFIG_SRCDIR(sysprof.glade)

AM_INIT_AUTOMAKE(no-define)

AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL

# Support silent build rules, requires at least automake-1.11. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

changequote(,)dnl
if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *[\ \	]-Wall[\ \	]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac
fi
changequote([,])dnl

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

# Kernel version
KMAJOR=`uname -r | cut -d"." -f 1`
KMINOR=`uname -r | cut -d"." -f 2`
KMICRO=`uname -r | cut -d"." -f 3 | cut -d"-" -f 1`

# Pkgconfig dependencies

core_dep="glib-2.0 >= 2.6.0"
gui_dep="gtk+-2.0 > 2.6.0 gdk-pixbuf-2.0 pangoft2 libglade-2.0"

PKG_CHECK_MODULES(CORE_DEP, $core_dep, [], AC_MSG_ERROR([sysprof dependencies not satisfied]))

build_gui=yes
PKG_CHECK_MODULES(GUI_DEP, $gui_dep, [], build_gui=no)

AM_CONDITIONAL([BUILD_GUI], [test "$build_gui" = yes])

# emit files

AC_SUBST(CORE_DEP_LIBS)
AC_SUBST(GUI_DEP_LIBS)
AC_SUBST(MODULE_SUBDIR)

AC_CONFIG_FILES([
Makefile
])

AC_OUTPUT

if [[ $KMICRO -lt 31 ]] ; then
   echo
   echo "%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%"
   echo "@"  
   echo "%      This version of sysprof will only work with a "
   echo "@      2.6.31 kernel or later, but you are using 2.6.$KMICRO."
   echo "%"
   echo "@      You can _compile_ it against this version, but it "
   echo "%      will not work unless you install 2.6.31 or later. "
   echo "@"
   echo "@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@%@"
   echo
fi
