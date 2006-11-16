AC_PREREQ(2.54)

AC_INIT([sysprof], [1.0.7])
AC_CONFIG_SRCDIR(sysprof.glade)

AM_INIT_AUTOMAKE(no-define)

AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_INSTALL

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

AC_ARG_ENABLE(kernel-module,
     AC_HELP_STRING(--disable-kernel-module, disable building kernel module))

kernel_module="yes"
if test "x$enableval" = "xno"; then
	kernel_module="no"
fi

if test $kernel_module = "yes"; then
  # Activate build in module/ subdir
  MODULE_SUBDIR=module
  
  # Kernel version
  KMAJOR=`uname -r | cut -d"." -f 1`
  KMINOR=`uname -r | cut -d"." -f 2`
  KMICRO=`uname -r | cut -d"." -f 3 | cut -d"-" -f 1`

  if [[ $KMICRO -lt 11 ]] ; then
     echo \*
     echo \* Linux \>= 2.6.11 is required
     echo \*
     exit 1
  fi

  if [ ! test -f /lib/modules/`uname -r`/build/Makefile ] ; then
     echo \*
     echo \* Sysprof requires the kernel source code to be installed.
     echo \* On a Fedora Core system the relevant package is kernel-devel
     echo \*
     exit 1
  fi
fi

# Pkgconfig dependencies
	
dep_modules="gtk+-2.0 > 2.6.0 gthread-2.0 gdk-pixbuf-2.0 pangoft2 libglade-2.0"

PKG_CHECK_MODULES(DEP, $dep_modules, [],
	          AC_MSG_ERROR([sysprof dependencies not satisfied]))

# libiberty and libbfd

AC_CHECK_LIB(iberty, cplus_demangle,:, 
  AC_CHECK_LIB(iberty, cplus_demangle_opname, [],
    AC_MSG_ERROR([libiberty is required to compile sysprof]), -ldl))

AC_CHECK_LIB(bfd, bfd_get_error, [DEP_LIBS="$DEP_LIBS -lbfd -liberty"],
  AC_MSG_ERROR([libbfd is required to compile sysprof]),
  -liberty)


# emit files

AC_SUBST(DEP_LIBS)
AC_SUBST(MODULE_SUBDIR)

AC_CONFIG_FILES([
Makefile
])

AC_OUTPUT
