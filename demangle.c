/*
 * "I bred them together to create a monster"
 *
 * This file is a concatenation of the files
 *
 *	cp-demangle.c
 *	cp-dmeangle.h
 *	cplus-dem.c
 *	demangle.h
 *
 * all taken from libiberty in binutils v. 2.16. After this concatenation
 * many calls to other functions in libiberty were replaced by calls to
 * similar functions in glib. Also global entry points that we don't need
 * in sysprof were made static or removed.
 *
 * Let's hope that no bugs are ever found in this file!
 *
 *	Maybe someday look at what can be deleted from this file
 *
 *	 - "mini string library" can be replaced with GString
 *	 - "option" parameter to cplus_demangle can be deleted
 *	 - demangling is always "auto"
 */

/* Copyright notices:
 *
 * Demangler for g++ V3 ABI.
 *  Copyright (C) 2003, 2004 Free Software Foundation, Inc.
 * Written by Ian Lance Taylor <ian@wasabisystems.com>.
 *
 * This file is part of the libiberty library, which is part of GCC.
 
   Defs for interface to demanglers.
   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1998, 2000, 2001, 2002,
   2003, 2004 Free Software Foundation, Inc.
   
   Internal demangler interface for g++ V3 ABI.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@wasabisystems.com>.

   Demangler for GNU C++
   Copyright 1989, 1991, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   
   Written by James Clark (jjc@jclark.uucp)
   Rewritten by Fred Fish (fnf@cygnus.com) for ARM and Lucid demangling
   Modified by Satish Pai (pai@apollo.hp.com) for HP demangling

   This file is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/


/* This code implements a demangler for the g++ V3 ABI.  The ABI is
   described on this web page:
       http://www.codesourcery.com/cxx-abi/abi.html#mangling

   This code was written while looking at the demangler written by
   Alex Samuel <samuel@codesourcery.com>.

   This code first pulls the mangled name apart into a list of
   components, and then walks the list generating the demangled
   name.

   This file will normally define the following functions, q.v.:
      char *cplus_demangle_v3(const char *mangled, int options)
      char *java_demangle_v3(const char *mangled)
      enum gnu_v3_ctor_kinds is_gnu_v3_mangled_ctor (const char *name)
      enum gnu_v3_dtor_kinds is_gnu_v3_mangled_dtor (const char *name)

   Also, the interface to the component list is public, and defined in
   demangle.h.  The interface consists of these types, which are
   defined in demangle.h:
      enum demangle_component_type
      struct demangle_component
   and these functions defined in this file:
      cplus_demangle_fill_name
      cplus_demangle_fill_extended_operator
      cplus_demangle_fill_ctor
      cplus_demangle_fill_dtor
      cplus_demangle_print
   and other functions defined in the file cp-demint.c.

   This file also defines some other functions and variables which are
   only to be used by the file cp-demint.c.

   Preprocessor macros you can define while compiling this file:

   IN_LIBGCC2
      If defined, this file defines the following function, q.v.:
         char *__cxa_demangle (const char *mangled, char *buf, size_t *len,
                               int *status)
      instead of cplus_demangle_v3() and java_demangle_v3().

   IN_GLIBCPP_V3
      If defined, this file defines only __cxa_demangle(), and no other
      publically visible functions or variables.

   CP_DEMANGLE_DEBUG
      If defined, turns on debugging mode, which prints information on
      stdout about the mangled string.  This is not generally useful.
*/

#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
/* Defs for interface to demanglers.
   Copyright 1992, 1993, 1994, 1995, 1996, 1997, 1998, 2000, 2001, 2002,
   2003, 2004 Free Software Foundation, Inc.
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */


#if !defined (DEMANGLE_H)
#define DEMANGLE_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Options passed to cplus_demangle (in 2nd parameter). */

#define DMGL_NO_OPTS	 0		/* For readability... */
#define DMGL_PARAMS	 (1 << 0)	/* Include function args */
#define DMGL_ANSI	 (1 << 1)	/* Include const, volatile, etc */
#define DMGL_JAVA	 (1 << 2)	/* Demangle as Java rather than C++. */
#define DMGL_VERBOSE	 (1 << 3)	/* Include implementation details.  */
#define DMGL_TYPES	 (1 << 4)	/* Also try to demangle type encodings.  */

#define DMGL_AUTO	 (1 << 8)
#define DMGL_GNU	 (1 << 9)
#define DMGL_LUCID	 (1 << 10)
#define DMGL_ARM	 (1 << 11)
#define DMGL_HP 	 (1 << 12)       /* For the HP aCC compiler;
                                            same as ARM except for
                                            template arguments, etc. */
#define DMGL_EDG	 (1 << 13)
#define DMGL_GNU_V3	 (1 << 14)
#define DMGL_GNAT	 (1 << 15)

/* If none of these are set, use 'current_demangling_style' as the default. */
#define DMGL_STYLE_MASK (DMGL_AUTO|DMGL_GNU|DMGL_LUCID|DMGL_ARM|DMGL_HP|DMGL_EDG|DMGL_GNU_V3|DMGL_JAVA|DMGL_GNAT)

/* Enumeration of possible demangling styles.

   Lucid and ARM styles are still kept logically distinct, even though
   they now both behave identically.  The resulting style is actual the
   union of both.  I.E. either style recognizes both "__pt__" and "__rf__"
   for operator "->", even though the first is lucid style and the second
   is ARM style. (FIXME?) */

enum demangling_styles
{
  no_demangling = -1,
  unknown_demangling = 0,
  auto_demangling = DMGL_AUTO,
  gnu_demangling = DMGL_GNU,
  lucid_demangling = DMGL_LUCID,
  arm_demangling = DMGL_ARM,
  hp_demangling = DMGL_HP,
  edg_demangling = DMGL_EDG,
  gnu_v3_demangling = DMGL_GNU_V3,
  java_demangling = DMGL_JAVA,
  gnat_demangling = DMGL_GNAT
};

/* Define string names for the various demangling styles. */

#define NO_DEMANGLING_STYLE_STRING            "none"
#define AUTO_DEMANGLING_STYLE_STRING	      "auto"
#define GNU_DEMANGLING_STYLE_STRING    	      "gnu"
#define LUCID_DEMANGLING_STYLE_STRING	      "lucid"
#define ARM_DEMANGLING_STYLE_STRING	      "arm"
#define HP_DEMANGLING_STYLE_STRING	      "hp"
#define EDG_DEMANGLING_STYLE_STRING	      "edg"
#define GNU_V3_DEMANGLING_STYLE_STRING        "gnu-v3"
#define JAVA_DEMANGLING_STYLE_STRING          "java"
#define GNAT_DEMANGLING_STYLE_STRING          "gnat"

/* Some macros to test what demangling style is active. */

#define CURRENT_DEMANGLING_STYLE current_demangling_style
#define AUTO_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_AUTO)
#define GNU_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_GNU)
#define LUCID_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_LUCID)
#define ARM_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_ARM)
#define HP_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_HP)
#define EDG_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_EDG)
#define GNU_V3_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_GNU_V3)
#define JAVA_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_JAVA)
#define GNAT_DEMANGLING (((int) CURRENT_DEMANGLING_STYLE) & DMGL_GNAT)

/* Provide information about the available demangle styles. This code is
   pulled from gdb into libiberty because it is useful to binutils also.  */

#define PARAMS(a) a

#define ATTRIBUTE_NORETURN  G_GNUC_NORETURN
#define ATTRIBUTE_UNUSED    G_GNUC_UNUSED
    
static const struct demangler_engine
{
  const char *const demangling_style_name;
  const enum demangling_styles demangling_style;
  const char *const demangling_style_doc;
} libiberty_demanglers[];

#if 0
extern char *
cplus_demangle PARAMS ((const char *mangled, int options));
#endif

#if 0
extern int
cplus_demangle_opname PARAMS ((const char *opname, char *result, int options));

extern const char *
cplus_mangle_opname PARAMS ((const char *opname, int options));
#endif

/* Note: This sets global state.  FIXME if you care about multi-threading. */

enum gnu_v3_ctor_kinds {
  gnu_v3_complete_object_ctor = 1,
  gnu_v3_base_object_ctor,
  gnu_v3_complete_object_allocating_ctor
};

/* Return non-zero iff NAME is the mangled form of a constructor name
   in the G++ V3 ABI demangling style.  Specifically, return an `enum
   gnu_v3_ctor_kinds' value indicating what kind of constructor
   it is.  */
extern enum gnu_v3_ctor_kinds
	is_gnu_v3_mangled_ctor PARAMS ((const char *name));


enum gnu_v3_dtor_kinds {
  gnu_v3_deleting_dtor = 1,
  gnu_v3_complete_object_dtor,
  gnu_v3_base_object_dtor
};

/* Return non-zero iff NAME is the mangled form of a destructor name
   in the G++ V3 ABI demangling style.  Specifically, return an `enum
   gnu_v3_dtor_kinds' value, indicating what kind of destructor
   it is.  */
extern enum gnu_v3_dtor_kinds
	is_gnu_v3_mangled_dtor PARAMS ((const char *name));

/* The V3 demangler works in two passes.  The first pass builds a tree
   representation of the mangled name, and the second pass turns the
   tree representation into a demangled string.  Here we define an
   interface to permit a caller to build their own tree
   representation, which they can pass to the demangler to get a
   demangled string.  This can be used to canonicalize user input into
   something which the demangler might output.  It could also be used
   by other demanglers in the future.  */

/* These are the component types which may be found in the tree.  Many
   component types have one or two subtrees, referred to as left and
   right (a component type with only one subtree puts it in the left
   subtree).  */

enum demangle_component_type
{
  /* A name, with a length and a pointer to a string.  */
  DEMANGLE_COMPONENT_NAME,
  /* A qualified name.  The left subtree is a class or namespace or
     some such thing, and the right subtree is a name qualified by
     that class.  */
  DEMANGLE_COMPONENT_QUAL_NAME,
  /* A local name.  The left subtree describes a function, and the
     right subtree is a name which is local to that function.  */
  DEMANGLE_COMPONENT_LOCAL_NAME,
  /* A typed name.  The left subtree is a name, and the right subtree
     describes that name as a function.  */
  DEMANGLE_COMPONENT_TYPED_NAME,
  /* A template.  The left subtree is a template name, and the right
     subtree is a template argument list.  */
  DEMANGLE_COMPONENT_TEMPLATE,
  /* A template parameter.  This holds a number, which is the template
     parameter index.  */
  DEMANGLE_COMPONENT_TEMPLATE_PARAM,
  /* A constructor.  This holds a name and the kind of
     constructor.  */
  DEMANGLE_COMPONENT_CTOR,
  /* A destructor.  This holds a name and the kind of destructor.  */
  DEMANGLE_COMPONENT_DTOR,
  /* A vtable.  This has one subtree, the type for which this is a
     vtable.  */
  DEMANGLE_COMPONENT_VTABLE,
  /* A VTT structure.  This has one subtree, the type for which this
     is a VTT.  */
  DEMANGLE_COMPONENT_VTT,
  /* A construction vtable.  The left subtree is the type for which
     this is a vtable, and the right subtree is the derived type for
     which this vtable is built.  */
  DEMANGLE_COMPONENT_CONSTRUCTION_VTABLE,
  /* A typeinfo structure.  This has one subtree, the type for which
     this is the tpeinfo structure.  */
  DEMANGLE_COMPONENT_TYPEINFO,
  /* A typeinfo name.  This has one subtree, the type for which this
     is the typeinfo name.  */
  DEMANGLE_COMPONENT_TYPEINFO_NAME,
  /* A typeinfo function.  This has one subtree, the type for which
     this is the tpyeinfo function.  */
  DEMANGLE_COMPONENT_TYPEINFO_FN,
  /* A thunk.  This has one subtree, the name for which this is a
     thunk.  */
  DEMANGLE_COMPONENT_THUNK,
  /* A virtual thunk.  This has one subtree, the name for which this
     is a virtual thunk.  */
  DEMANGLE_COMPONENT_VIRTUAL_THUNK,
  /* A covariant thunk.  This has one subtree, the name for which this
     is a covariant thunk.  */
  DEMANGLE_COMPONENT_COVARIANT_THUNK,
  /* A Java class.  This has one subtree, the type.  */
  DEMANGLE_COMPONENT_JAVA_CLASS,
  /* A guard variable.  This has one subtree, the name for which this
     is a guard variable.  */
  DEMANGLE_COMPONENT_GUARD,
  /* A reference temporary.  This has one subtree, the name for which
     this is a temporary.  */
  DEMANGLE_COMPONENT_REFTEMP,
  /* A standard substitution.  This holds the name of the
     substitution.  */
  DEMANGLE_COMPONENT_SUB_STD,
  /* The restrict qualifier.  The one subtree is the type which is
     being qualified.  */
  DEMANGLE_COMPONENT_RESTRICT,
  /* The volatile qualifier.  The one subtree is the type which is
     being qualified.  */
  DEMANGLE_COMPONENT_VOLATILE,
  /* The const qualifier.  The one subtree is the type which is being
     qualified.  */
  DEMANGLE_COMPONENT_CONST,
  /* The restrict qualifier modifying a member function.  The one
     subtree is the type which is being qualified.  */
  DEMANGLE_COMPONENT_RESTRICT_THIS,
  /* The volatile qualifier modifying a member function.  The one
     subtree is the type which is being qualified.  */
  DEMANGLE_COMPONENT_VOLATILE_THIS,
  /* The const qualifier modifying a member function.  The one subtree
     is the type which is being qualified.  */
  DEMANGLE_COMPONENT_CONST_THIS,
  /* A vendor qualifier.  The left subtree is the type which is being
     qualified, and the right subtree is the name of the
     qualifier.  */
  DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL,
  /* A pointer.  The one subtree is the type which is being pointed
     to.  */
  DEMANGLE_COMPONENT_POINTER,
  /* A reference.  The one subtree is the type which is being
     referenced.  */
  DEMANGLE_COMPONENT_REFERENCE,
  /* A complex type.  The one subtree is the base type.  */
  DEMANGLE_COMPONENT_COMPLEX,
  /* An imaginary type.  The one subtree is the base type.  */
  DEMANGLE_COMPONENT_IMAGINARY,
  /* A builtin type.  This holds the builtin type information.  */
  DEMANGLE_COMPONENT_BUILTIN_TYPE,
  /* A vendor's builtin type.  This holds the name of the type.  */
  DEMANGLE_COMPONENT_VENDOR_TYPE,
  /* A function type.  The left subtree is the return type.  The right
     subtree is a list of ARGLIST nodes.  Either or both may be
     NULL.  */
  DEMANGLE_COMPONENT_FUNCTION_TYPE,
  /* An array type.  The left subtree is the dimension, which may be
     NULL, or a string (represented as DEMANGLE_COMPONENT_NAME), or an
     expression.  The right subtree is the element type.  */
  DEMANGLE_COMPONENT_ARRAY_TYPE,
  /* A pointer to member type.  The left subtree is the class type,
     and the right subtree is the member type.  CV-qualifiers appear
     on the latter.  */
  DEMANGLE_COMPONENT_PTRMEM_TYPE,
  /* An argument list.  The left subtree is the current argument, and
     the right subtree is either NULL or another ARGLIST node.  */
  DEMANGLE_COMPONENT_ARGLIST,
  /* A template argument list.  The left subtree is the current
     template argument, and the right subtree is either NULL or
     another TEMPLATE_ARGLIST node.  */
  DEMANGLE_COMPONENT_TEMPLATE_ARGLIST,
  /* An operator.  This holds information about a standard
     operator.  */
  DEMANGLE_COMPONENT_OPERATOR,
  /* An extended operator.  This holds the number of arguments, and
     the name of the extended operator.  */
  DEMANGLE_COMPONENT_EXTENDED_OPERATOR,
  /* A typecast, represented as a unary operator.  The one subtree is
     the type to which the argument should be cast.  */
  DEMANGLE_COMPONENT_CAST,
  /* A unary expression.  The left subtree is the operator, and the
     right subtree is the single argument.  */
  DEMANGLE_COMPONENT_UNARY,
  /* A binary expression.  The left subtree is the operator, and the
     right subtree is a BINARY_ARGS.  */
  DEMANGLE_COMPONENT_BINARY,
  /* Arguments to a binary expression.  The left subtree is the first
     argument, and the right subtree is the second argument.  */
  DEMANGLE_COMPONENT_BINARY_ARGS,
  /* A trinary expression.  The left subtree is the operator, and the
     right subtree is a TRINARY_ARG1.  */
  DEMANGLE_COMPONENT_TRINARY,
  /* Arguments to a trinary expression.  The left subtree is the first
     argument, and the right subtree is a TRINARY_ARG2.  */
  DEMANGLE_COMPONENT_TRINARY_ARG1,
  /* More arguments to a trinary expression.  The left subtree is the
     second argument, and the right subtree is the third argument.  */
  DEMANGLE_COMPONENT_TRINARY_ARG2,
  /* A literal.  The left subtree is the type, and the right subtree
     is the value, represented as a DEMANGLE_COMPONENT_NAME.  */
  DEMANGLE_COMPONENT_LITERAL,
  /* A negative literal.  Like LITERAL, but the value is negated.
     This is a minor hack: the NAME used for LITERAL points directly
     to the mangled string, but since negative numbers are mangled
     using 'n' instead of '-', we want a way to indicate a negative
     number which involves neither modifying the mangled string nor
     allocating a new copy of the literal in memory.  */
  DEMANGLE_COMPONENT_LITERAL_NEG
};

/* Types which are only used internally.  */

struct demangle_operator_info;
struct demangle_builtin_type_info;

/* A node in the tree representation is an instance of a struct
   demangle_component.  Note that the field names of the struct are
   not well protected against macros defined by the file including
   this one.  We can fix this if it ever becomes a problem.  */

struct demangle_component
{
  /* The type of this component.  */
  enum demangle_component_type type;

  union
  {
    /* For DEMANGLE_COMPONENT_NAME.  */
    struct
    {
      /* A pointer to the name (which need not NULL terminated) and
	 its length.  */
      const char *s;
      int len;
    } s_name;

    /* For DEMANGLE_COMPONENT_OPERATOR.  */
    struct
    {
      /* Operator.  */
      const struct demangle_operator_info *op;
    } s_operator;

    /* For DEMANGLE_COMPONENT_EXTENDED_OPERATOR.  */
    struct
    {
      /* Number of arguments.  */
      int args;
      /* Name.  */
      struct demangle_component *name;
    } s_extended_operator;

    /* For DEMANGLE_COMPONENT_CTOR.  */
    struct
    {
      /* Kind of constructor.  */
      enum gnu_v3_ctor_kinds kind;
      /* Name.  */
      struct demangle_component *name;
    } s_ctor;

    /* For DEMANGLE_COMPONENT_DTOR.  */
    struct
    {
      /* Kind of destructor.  */
      enum gnu_v3_dtor_kinds kind;
      /* Name.  */
      struct demangle_component *name;
    } s_dtor;

    /* For DEMANGLE_COMPONENT_BUILTIN_TYPE.  */
    struct
    {
      /* Builtin type.  */
      const struct demangle_builtin_type_info *type;
    } s_builtin;

    /* For DEMANGLE_COMPONENT_SUB_STD.  */
    struct
    {
      /* Standard substitution string.  */
      const char* string;
      /* Length of string.  */
      int len;
    } s_string;

    /* For DEMANGLE_COMPONENT_TEMPLATE_PARAM.  */
    struct
    {
      /* Template parameter index.  */
      long number;
    } s_number;

    /* For other types.  */
    struct
    {
      /* Left (or only) subtree.  */
      struct demangle_component *left;
      /* Right subtree.  */
      struct demangle_component *right;
    } s_binary;

  } u;
};

/* People building mangled trees are expected to allocate instances of
   struct demangle_component themselves.  They can then call one of
   the following functions to fill them in.  */

/* Fill in most component types with a left subtree and a right
   subtree.  Returns non-zero on success, zero on failure, such as an
   unrecognized or inappropriate component type.  */

extern int
cplus_demangle_fill_component PARAMS ((struct demangle_component *fill,
				       enum demangle_component_type,
				       struct demangle_component *left,
				       struct demangle_component *right));

/* Fill in a DEMANGLE_COMPONENT_NAME.  Returns non-zero on success,
   zero for bad arguments.  */

extern int
cplus_demangle_fill_name PARAMS ((struct demangle_component *fill,
				  const char *, int));

/* Fill in a DEMANGLE_COMPONENT_BUILTIN_TYPE, using the name of the
   builtin type (e.g., "int", etc.).  Returns non-zero on success,
   zero if the type is not recognized.  */

extern int
cplus_demangle_fill_builtin_type PARAMS ((struct demangle_component *fill,
					  const char *type_name));

/* Fill in a DEMANGLE_COMPONENT_OPERATOR, using the name of the
   operator and the number of arguments which it takes (the latter is
   used to disambiguate operators which can be both binary and unary,
   such as '-').  Returns non-zero on success, zero if the operator is
   not recognized.  */

extern int
cplus_demangle_fill_operator PARAMS ((struct demangle_component *fill,
				      const char *opname, int args));

/* Fill in a DEMANGLE_COMPONENT_EXTENDED_OPERATOR, providing the
   number of arguments and the name.  Returns non-zero on success,
   zero for bad arguments.  */

extern int
cplus_demangle_fill_extended_operator PARAMS ((struct demangle_component *fill,
					       int numargs,
					       struct demangle_component *nm));

/* Fill in a DEMANGLE_COMPONENT_CTOR.  Returns non-zero on success,
   zero for bad arguments.  */

extern int
cplus_demangle_fill_ctor PARAMS ((struct demangle_component *fill,
				  enum gnu_v3_ctor_kinds kind,
				  struct demangle_component *name));

/* Fill in a DEMANGLE_COMPONENT_DTOR.  Returns non-zero on success,
   zero for bad arguments.  */

extern int
cplus_demangle_fill_dtor PARAMS ((struct demangle_component *fill,
				  enum gnu_v3_dtor_kinds kind,
				  struct demangle_component *name));

/* This function translates a mangled name into a struct
   demangle_component tree.  The first argument is the mangled name.
   The second argument is DMGL_* options.  This returns a pointer to a
   tree on success, or NULL on failure.  On success, the third
   argument is set to a block of memory allocated by malloc.  This
   block should be passed to free when the tree is no longer
   needed.  */

extern struct demangle_component *
cplus_demangle_v3_components PARAMS ((const char *mangled,
				      int options,
				      void **mem));

/* This function takes a struct demangle_component tree and returns
   the corresponding demangled string.  The first argument is DMGL_*
   options.  The second is the tree to demangle.  The third is a guess
   at the length of the demangled string, used to initially allocate
   the return buffer.  The fourth is a pointer to a size_t.  On
   success, this function returns a buffer allocated by malloc(), and
   sets the size_t pointed to by the fourth argument to the size of
   the allocated buffer (not the length of the returned string).  On
   failure, this function returns NULL, and sets the size_t pointed to
   by the fourth argument to 0 for an invalid tree, or to 1 for a
   memory allocation error.  */

extern char *
cplus_demangle_print PARAMS ((int options,
			      const struct demangle_component *tree,
			      int estimated_length,
			      size_t *p_allocated_size));

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif	/* DEMANGLE_H */


/* V3 ABI demangling entry points, defined in cp-demangle.c.  */
static char* cplus_demangle_v3 PARAMS ((const char* mangled, int options));

static char* java_demangle_v3 PARAMS ((const char* mangled));

/* Internal demangler interface for g++ V3 ABI.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@wasabisystems.com>.

   This file is part of the libiberty library, which is part of GCC.

   This file is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   In addition to the permissions in the GNU General Public License, the
   Free Software Foundation gives you unlimited permission to link the
   compiled version of this file into combinations with other programs,
   and to distribute those combinations without any restriction coming
   from the use of this file.  (The General Public License restrictions
   do apply in other respects; for example, they cover modification of
   the file, and distribution when not linked into a combined
   executable.)

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA. 
*/

/* This file provides some definitions shared by cp-demangle.c and
   cp-demint.c.  It should not be included by any other files.  */

/* Information we keep for operators.  */

struct demangle_operator_info
{
  /* Mangled name.  */
  const char *code;
  /* Real name.  */
  const char *name;
  /* Length of real name.  */
  int len;
  /* Number of arguments.  */
  int args;
};

/* How to print the value of a builtin type.  */

enum d_builtin_type_print
{
  /* Print as (type)val.  */
  D_PRINT_DEFAULT,
  /* Print as integer.  */
  D_PRINT_INT,
  /* Print as unsigned integer, with trailing "u".  */
  D_PRINT_UNSIGNED,
  /* Print as long, with trailing "l".  */
  D_PRINT_LONG,
  /* Print as unsigned long, with trailing "ul".  */
  D_PRINT_UNSIGNED_LONG,
  /* Print as long long, with trailing "ll".  */
  D_PRINT_LONG_LONG,
  /* Print as unsigned long long, with trailing "ull".  */
  D_PRINT_UNSIGNED_LONG_LONG,
  /* Print as bool.  */
  D_PRINT_BOOL,
  /* Print as float--put value in square brackets.  */
  D_PRINT_FLOAT,
  /* Print in usual way, but here to detect void.  */
  D_PRINT_VOID
};

/* Information we keep for a builtin type.  */

struct demangle_builtin_type_info
{
  /* Type name.  */
  const char *name;
  /* Length of type name.  */
  int len;
  /* Type name when using Java.  */
  const char *java_name;
  /* Length of java name.  */
  int java_len;
  /* How to print a value of this type.  */
  enum d_builtin_type_print print;
};

/* The information structure we pass around.  */

struct d_info
{
  /* The string we are demangling.  */
  const char *s;
  /* The end of the string we are demangling.  */
  const char *send;
  /* The options passed to the demangler.  */
  int options;
  /* The next character in the string to consider.  */
  const char *n;
  /* The array of components.  */
  struct demangle_component *comps;
  /* The index of the next available component.  */
  int next_comp;
  /* The number of available component structures.  */
  int num_comps;
  /* The array of substitutions.  */
  struct demangle_component **subs;
  /* The index of the next substitution.  */
  int next_sub;
  /* The number of available entries in the subs array.  */
  int num_subs;
  /* The number of substitutions which we actually made from the subs
     array, plus the number of template parameter references we
     saw.  */
  int did_subs;
  /* The last name we saw, for constructors and destructors.  */
  struct demangle_component *last_name;
  /* A running total of the length of large expansions from the
     mangled name to the demangled name, such as standard
     substitutions and builtin types.  */
  int expansion;
};

#define d_peek_char(di) (*((di)->n))
#define d_peek_next_char(di) ((di)->n[1])
#define d_advance(di, i) ((di)->n += (i))
#define d_next_char(di) (*((di)->n++))
#define d_str(di) ((di)->n)

/* Functions and arrays in cp-demangle.c which are referenced by
   functions in cp-demint.c.  */
#define CP_STATIC_IF_GLIBCPP_V3 static

#define D_BUILTIN_TYPE_COUNT (26)

#if 0
static struct demangle_component *
cplus_demangle_mangled_name PARAMS ((struct d_info *, int));
#endif

#if 0
static struct demangle_component *
cplus_demangle_type PARAMS ((struct d_info *));
#endif

extern void
cplus_demangle_init_info PARAMS ((const char *, int, size_t, struct d_info *));

#if 0
/* cp-demangle.c needs to define this a little differently */
#undef CP_STATIC_IF_GLIBCPP_V3
#endif
#define IN_GLIBCPP_V3

/* If IN_GLIBCPP_V3 is defined, some functions are made static.  We
   also rename them via #define to avoid compiler errors when the
   static definition conflicts with the extern declaration in a header
   file.  */
#ifdef IN_GLIBCPP_V3

#define CP_STATIC_IF_GLIBCPP_V3 static

#define cplus_demangle_fill_name d_fill_name
static int
d_fill_name PARAMS ((struct demangle_component *, const char *, int));

#define cplus_demangle_fill_extended_operator d_fill_extended_operator
static int
d_fill_extended_operator PARAMS ((struct demangle_component *, int,
				  struct demangle_component *));

#define cplus_demangle_fill_ctor d_fill_ctor
static int
d_fill_ctor PARAMS ((struct demangle_component *, enum gnu_v3_ctor_kinds,
		     struct demangle_component *));

#define cplus_demangle_fill_dtor d_fill_dtor
static int
d_fill_dtor PARAMS ((struct demangle_component *, enum gnu_v3_dtor_kinds,
		     struct demangle_component *));

#define cplus_demangle_mangled_name d_mangled_name
static struct demangle_component *
d_mangled_name PARAMS ((struct d_info *, int));

#define cplus_demangle_type d_type
static struct demangle_component *
d_type PARAMS ((struct d_info *));

#define cplus_demangle_print d_print
static char *
d_print PARAMS ((int, const struct demangle_component *, int, size_t *));

#define cplus_demangle_init_info d_init_info
static void
d_init_info PARAMS ((const char *, int, size_t, struct d_info *));

#else /* ! defined(IN_GLIBCPP_V3) */
#define CP_STATIC_IF_GLIBCPP_V3
#endif /* ! defined(IN_GLIBCPP_V3) */

/* See if the compiler supports dynamic arrays.  */

#ifdef __GNUC__
#define CP_DYNAMIC_ARRAYS
#else
#ifdef __STDC__
#ifdef __STDC_VERSION__
#if __STDC_VERSION__ >= 199901L
#define CP_DYNAMIC_ARRAYS
#endif /* __STDC__VERSION >= 199901L */
#endif /* defined (__STDC_VERSION__) */
#endif /* defined (__STDC__) */
#endif /* ! defined (__GNUC__) */

/* We avoid pulling in the ctype tables, to prevent pulling in
   additional unresolved symbols when this code is used in a library.
   FIXME: Is this really a valid reason?  This comes from the original
   V3 demangler code.

   As of this writing this file has the following undefined references
   when compiled with -DIN_GLIBCPP_V3: malloc, realloc, free, memcpy,
   strcpy, strcat, strlen.  */

#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_UPPER(c) ((c) >= 'A' && (c) <= 'Z')
#define IS_LOWER(c) ((c) >= 'a' && (c) <= 'z')

/* The prefix prepended by GCC to an identifier represnting the
   anonymous namespace.  */
#define ANONYMOUS_NAMESPACE_PREFIX "_GLOBAL_"
#define ANONYMOUS_NAMESPACE_PREFIX_LEN \
  (sizeof (ANONYMOUS_NAMESPACE_PREFIX) - 1)

/* Information we keep for the standard substitutions.  */

struct d_standard_sub_info
{
  /* The code for this substitution.  */
  char code;
  /* The simple string it expands to.  */
  const char *simple_expansion;
  /* The length of the simple expansion.  */
  int simple_len;
  /* The results of a full, verbose, expansion.  This is used when
     qualifying a constructor/destructor, or when in verbose mode.  */
  const char *full_expansion;
  /* The length of the full expansion.  */
  int full_len;
  /* What to set the last_name field of d_info to; NULL if we should
     not set it.  This is only relevant when qualifying a
     constructor/destructor.  */
  const char *set_last_name;
  /* The length of set_last_name.  */
  int set_last_name_len;
};

/* Accessors for subtrees of struct demangle_component.  */

#define d_left(dc) ((dc)->u.s_binary.left)
#define d_right(dc) ((dc)->u.s_binary.right)

/* A list of templates.  This is used while printing.  */

struct d_print_template
{
  /* Next template on the list.  */
  struct d_print_template *next;
  /* This template.  */
  const struct demangle_component *template;
};

/* A list of type modifiers.  This is used while printing.  */

struct d_print_mod
{
  /* Next modifier on the list.  These are in the reverse of the order
     in which they appeared in the mangled string.  */
  struct d_print_mod *next;
  /* The modifier.  */
  const struct demangle_component *mod;
  /* Whether this modifier was printed.  */
  int printed;
  /* The list of templates which applies to this modifier.  */
  struct d_print_template *templates;
};

/* We use this structure to hold information during printing.  */

struct d_print_info
{
  /* The options passed to the demangler.  */
  int options;
  /* Buffer holding the result.  */
  char *buf;
  /* Current length of data in buffer.  */
  size_t len;
  /* Allocated size of buffer.  */
  size_t alc;
  /* The current list of templates, if any.  */
  struct d_print_template *templates;
  /* The current list of modifiers (e.g., pointer, reference, etc.),
     if any.  */
  struct d_print_mod *modifiers;
  /* Set to 1 if we had a memory allocation failure.  */
  int allocation_failure;
};

#define d_print_saw_error(dpi) ((dpi)->buf == NULL)

#define d_append_char(dpi, c) \
  do \
    { \
      if ((dpi)->buf != NULL && (dpi)->len < (dpi)->alc) \
        (dpi)->buf[(dpi)->len++] = (c); \
      else \
        d_print_append_char ((dpi), (c)); \
    } \
  while (0)

#define d_append_buffer(dpi, s, l) \
  do \
    { \
      if ((dpi)->buf != NULL && (dpi)->len + (l) <= (dpi)->alc) \
        { \
          memcpy ((dpi)->buf + (dpi)->len, (s), (l)); \
          (dpi)->len += l; \
        } \
      else \
        d_print_append_buffer ((dpi), (s), (l)); \
    } \
  while (0)

#define d_append_string_constant(dpi, s) \
  d_append_buffer (dpi, (s), sizeof (s) - 1)

#define d_last_char(dpi) \
  ((dpi)->buf == NULL || (dpi)->len == 0 ? '\0' : (dpi)->buf[(dpi)->len - 1])

#ifdef CP_DEMANGLE_DEBUG
static void 
d_dump PARAMS ((struct demangle_component *, int));
#endif

static struct demangle_component *
d_make_empty PARAMS ((struct d_info *));

static struct demangle_component *
d_make_comp PARAMS ((struct d_info *, enum demangle_component_type,
		     struct demangle_component *,
		     struct demangle_component *));

static struct demangle_component *
d_make_name PARAMS ((struct d_info *, const char *, int));

static struct demangle_component *
d_make_builtin_type PARAMS ((struct d_info *,
			     const struct demangle_builtin_type_info *));

static struct demangle_component *
d_make_operator PARAMS ((struct d_info *,
			 const struct demangle_operator_info *));

static struct demangle_component *
d_make_extended_operator PARAMS ((struct d_info *, int,
				  struct demangle_component *));

static struct demangle_component *
d_make_ctor PARAMS ((struct d_info *, enum gnu_v3_ctor_kinds,
		     struct demangle_component *));

static struct demangle_component *
d_make_dtor PARAMS ((struct d_info *, enum gnu_v3_dtor_kinds,
		     struct demangle_component *));

static struct demangle_component *
d_make_template_param PARAMS ((struct d_info *, long));

static struct demangle_component *
d_make_sub PARAMS ((struct d_info *, const char *, int));

static int
has_return_type PARAMS ((struct demangle_component *));

static int
is_ctor_dtor_or_conversion PARAMS ((struct demangle_component *));

static struct demangle_component *
d_encoding PARAMS ((struct d_info *, int));

static struct demangle_component *
d_name PARAMS ((struct d_info *));

static struct demangle_component *
d_nested_name PARAMS ((struct d_info *));

static struct demangle_component *
d_prefix PARAMS ((struct d_info *));

static struct demangle_component *
d_unqualified_name PARAMS ((struct d_info *));

static struct demangle_component *
d_source_name PARAMS ((struct d_info *));

static long
d_number PARAMS ((struct d_info *));

static struct demangle_component *
d_identifier PARAMS ((struct d_info *, int));

static struct demangle_component *
d_operator_name PARAMS ((struct d_info *));

static struct demangle_component *
d_special_name PARAMS ((struct d_info *));

static int
d_call_offset PARAMS ((struct d_info *, int));

static struct demangle_component *
d_ctor_dtor_name PARAMS ((struct d_info *));

static struct demangle_component **
d_cv_qualifiers PARAMS ((struct d_info *, struct demangle_component **, int));

static struct demangle_component *
d_function_type PARAMS ((struct d_info *));

static struct demangle_component *
d_bare_function_type PARAMS ((struct d_info *, int));

static struct demangle_component *
d_class_enum_type PARAMS ((struct d_info *));

static struct demangle_component *
d_array_type PARAMS ((struct d_info *));

static struct demangle_component *
d_pointer_to_member_type PARAMS ((struct d_info *));

static struct demangle_component *
d_template_param PARAMS ((struct d_info *));

static struct demangle_component *
d_template_args PARAMS ((struct d_info *));

static struct demangle_component *
d_template_arg PARAMS ((struct d_info *));

static struct demangle_component *
d_expression PARAMS ((struct d_info *));

static struct demangle_component *
d_expr_primary PARAMS ((struct d_info *));

static struct demangle_component *
d_local_name PARAMS ((struct d_info *));

static int
d_discriminator PARAMS ((struct d_info *));

static int
d_add_substitution PARAMS ((struct d_info *, struct demangle_component *));

static struct demangle_component *
d_substitution PARAMS ((struct d_info *, int));

static void
d_print_resize PARAMS ((struct d_print_info *, size_t));

static void
d_print_append_char PARAMS ((struct d_print_info *, int));

static void
d_print_append_buffer PARAMS ((struct d_print_info *, const char *, size_t));

static void
d_print_error PARAMS ((struct d_print_info *));

static void
d_print_comp PARAMS ((struct d_print_info *,
		      const struct demangle_component *));

static void
d_print_java_identifier PARAMS ((struct d_print_info *, const char *, int));

static void
d_print_mod_list PARAMS ((struct d_print_info *, struct d_print_mod *, int));

static void
d_print_mod PARAMS ((struct d_print_info *,
		     const struct demangle_component *));

static void
d_print_function_type PARAMS ((struct d_print_info *,
			       const struct demangle_component *,
			       struct d_print_mod *));

static void
d_print_array_type PARAMS ((struct d_print_info *,
			    const struct demangle_component *,
			    struct d_print_mod *));

static void
d_print_expr_op PARAMS ((struct d_print_info *,
			 const struct demangle_component *));

static void
d_print_cast PARAMS ((struct d_print_info *,
		      const struct demangle_component *));

static char *
d_demangle PARAMS ((const char *, int, size_t *));

#ifdef CP_DEMANGLE_DEBUG

static void
d_dump (dc, indent)
     struct demangle_component *dc;
     int indent;
{
  int i;

  if (dc == NULL)
    return;

  for (i = 0; i < indent; ++i)
    putchar (' ');

  switch (dc->type)
    {
    case DEMANGLE_COMPONENT_NAME:
      printf ("name '%.*s'\n", dc->u.s_name.len, dc->u.s_name.s);
      return;
    case DEMANGLE_COMPONENT_TEMPLATE_PARAM:
      printf ("template parameter %ld\n", dc->u.s_number.number);
      return;
    case DEMANGLE_COMPONENT_CTOR:
      printf ("constructor %d\n", (int) dc->u.s_ctor.kind);
      d_dump (dc->u.s_ctor.name, indent + 2);
      return;
    case DEMANGLE_COMPONENT_DTOR:
      printf ("destructor %d\n", (int) dc->u.s_dtor.kind);
      d_dump (dc->u.s_dtor.name, indent + 2);
      return;
    case DEMANGLE_COMPONENT_SUB_STD:
      printf ("standard substitution %s\n", dc->u.s_string.string);
      return;
    case DEMANGLE_COMPONENT_BUILTIN_TYPE:
      printf ("builtin type %s\n", dc->u.s_builtin.type->name);
      return;
    case DEMANGLE_COMPONENT_OPERATOR:
      printf ("operator %s\n", dc->u.s_operator.op->name);
      return;
    case DEMANGLE_COMPONENT_EXTENDED_OPERATOR:
      printf ("extended operator with %d args\n",
	      dc->u.s_extended_operator.args);
      d_dump (dc->u.s_extended_operator.name, indent + 2);
      return;

    case DEMANGLE_COMPONENT_QUAL_NAME:
      printf ("qualified name\n");
      break;
    case DEMANGLE_COMPONENT_LOCAL_NAME:
      printf ("local name\n");
      break;
    case DEMANGLE_COMPONENT_TYPED_NAME:
      printf ("typed name\n");
      break;
    case DEMANGLE_COMPONENT_TEMPLATE:
      printf ("template\n");
      break;
    case DEMANGLE_COMPONENT_VTABLE:
      printf ("vtable\n");
      break;
    case DEMANGLE_COMPONENT_VTT:
      printf ("VTT\n");
      break;
    case DEMANGLE_COMPONENT_CONSTRUCTION_VTABLE:
      printf ("construction vtable\n");
      break;
    case DEMANGLE_COMPONENT_TYPEINFO:
      printf ("typeinfo\n");
      break;
    case DEMANGLE_COMPONENT_TYPEINFO_NAME:
      printf ("typeinfo name\n");
      break;
    case DEMANGLE_COMPONENT_TYPEINFO_FN:
      printf ("typeinfo function\n");
      break;
    case DEMANGLE_COMPONENT_THUNK:
      printf ("thunk\n");
      break;
    case DEMANGLE_COMPONENT_VIRTUAL_THUNK:
      printf ("virtual thunk\n");
      break;
    case DEMANGLE_COMPONENT_COVARIANT_THUNK:
      printf ("covariant thunk\n");
      break;
    case DEMANGLE_COMPONENT_JAVA_CLASS:
      printf ("java class\n");
      break;
    case DEMANGLE_COMPONENT_GUARD:
      printf ("guard\n");
      break;
    case DEMANGLE_COMPONENT_REFTEMP:
      printf ("reference temporary\n");
      break;
    case DEMANGLE_COMPONENT_RESTRICT:
      printf ("restrict\n");
      break;
    case DEMANGLE_COMPONENT_VOLATILE:
      printf ("volatile\n");
      break;
    case DEMANGLE_COMPONENT_CONST:
      printf ("const\n");
      break;
    case DEMANGLE_COMPONENT_RESTRICT_THIS:
      printf ("restrict this\n");
      break;
    case DEMANGLE_COMPONENT_VOLATILE_THIS:
      printf ("volatile this\n");
      break;
    case DEMANGLE_COMPONENT_CONST_THIS:
      printf ("const this\n");
      break;
    case DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL:
      printf ("vendor type qualifier\n");
      break;
    case DEMANGLE_COMPONENT_POINTER:
      printf ("pointer\n");
      break;
    case DEMANGLE_COMPONENT_REFERENCE:
      printf ("reference\n");
      break;
    case DEMANGLE_COMPONENT_COMPLEX:
      printf ("complex\n");
      break;
    case DEMANGLE_COMPONENT_IMAGINARY:
      printf ("imaginary\n");
      break;
    case DEMANGLE_COMPONENT_VENDOR_TYPE:
      printf ("vendor type\n");
      break;
    case DEMANGLE_COMPONENT_FUNCTION_TYPE:
      printf ("function type\n");
      break;
    case DEMANGLE_COMPONENT_ARRAY_TYPE:
      printf ("array type\n");
      break;
    case DEMANGLE_COMPONENT_PTRMEM_TYPE:
      printf ("pointer to member type\n");
      break;
    case DEMANGLE_COMPONENT_ARGLIST:
      printf ("argument list\n");
      break;
    case DEMANGLE_COMPONENT_TEMPLATE_ARGLIST:
      printf ("template argument list\n");
      break;
    case DEMANGLE_COMPONENT_CAST:
      printf ("cast\n");
      break;
    case DEMANGLE_COMPONENT_UNARY:
      printf ("unary operator\n");
      break;
    case DEMANGLE_COMPONENT_BINARY:
      printf ("binary operator\n");
      break;
    case DEMANGLE_COMPONENT_BINARY_ARGS:
      printf ("binary operator arguments\n");
      break;
    case DEMANGLE_COMPONENT_TRINARY:
      printf ("trinary operator\n");
      break;
    case DEMANGLE_COMPONENT_TRINARY_ARG1:
      printf ("trinary operator arguments 1\n");
      break;
    case DEMANGLE_COMPONENT_TRINARY_ARG2:
      printf ("trinary operator arguments 1\n");
      break;
    case DEMANGLE_COMPONENT_LITERAL:
      printf ("literal\n");
      break;
    case DEMANGLE_COMPONENT_LITERAL_NEG:
      printf ("negative literal\n");
      break;
    }

  d_dump (d_left (dc), indent + 2);
  d_dump (d_right (dc), indent + 2);
}

#endif /* CP_DEMANGLE_DEBUG */

/* Fill in a DEMANGLE_COMPONENT_NAME.  */

CP_STATIC_IF_GLIBCPP_V3
int
cplus_demangle_fill_name (p, s, len)
     struct demangle_component *p;
     const char *s;
     int len;
{
  if (p == NULL || s == NULL || len == 0)
    return 0;
  p->type = DEMANGLE_COMPONENT_NAME;
  p->u.s_name.s = s;
  p->u.s_name.len = len;
  return 1;
}

/* Fill in a DEMANGLE_COMPONENT_EXTENDED_OPERATOR.  */

CP_STATIC_IF_GLIBCPP_V3
int
cplus_demangle_fill_extended_operator (p, args, name)
     struct demangle_component *p;
     int args;
     struct demangle_component *name;
{
  if (p == NULL || args < 0 || name == NULL)
    return 0;
  p->type = DEMANGLE_COMPONENT_EXTENDED_OPERATOR;
  p->u.s_extended_operator.args = args;
  p->u.s_extended_operator.name = name;
  return 1;
}

/* Fill in a DEMANGLE_COMPONENT_CTOR.  */

CP_STATIC_IF_GLIBCPP_V3
int
cplus_demangle_fill_ctor (p, kind, name)
     struct demangle_component *p;
     enum gnu_v3_ctor_kinds kind;
     struct demangle_component *name;
{
  if (p == NULL
      || name == NULL
      || (kind < gnu_v3_complete_object_ctor
	  && kind > gnu_v3_complete_object_allocating_ctor))
    return 0;
  p->type = DEMANGLE_COMPONENT_CTOR;
  p->u.s_ctor.kind = kind;
  p->u.s_ctor.name = name;
  return 1;
}

/* Fill in a DEMANGLE_COMPONENT_DTOR.  */

CP_STATIC_IF_GLIBCPP_V3
int
cplus_demangle_fill_dtor (p, kind, name)
     struct demangle_component *p;
     enum gnu_v3_dtor_kinds kind;
     struct demangle_component *name;
{
  if (p == NULL
      || name == NULL
      || (kind < gnu_v3_deleting_dtor
	  && kind > gnu_v3_base_object_dtor))
    return 0;
  p->type = DEMANGLE_COMPONENT_DTOR;
  p->u.s_dtor.kind = kind;
  p->u.s_dtor.name = name;
  return 1;
}

/* Add a new component.  */

static struct demangle_component *
d_make_empty (di)
     struct d_info *di;
{
  struct demangle_component *p;

  if (di->next_comp >= di->num_comps)
    return NULL;
  p = &di->comps[di->next_comp];
  ++di->next_comp;
  return p;
}

/* Add a new generic component.  */

static struct demangle_component *
d_make_comp (di, type, left, right)
     struct d_info *di;
     enum demangle_component_type type;
     struct demangle_component *left;
     struct demangle_component *right;
{
  struct demangle_component *p;

  /* We check for errors here.  A typical error would be a NULL return
     from a subroutine.  We catch those here, and return NULL
     upward.  */
  switch (type)
    {
      /* These types require two parameters.  */
    case DEMANGLE_COMPONENT_QUAL_NAME:
    case DEMANGLE_COMPONENT_LOCAL_NAME:
    case DEMANGLE_COMPONENT_TYPED_NAME:
    case DEMANGLE_COMPONENT_TEMPLATE:
    case DEMANGLE_COMPONENT_CONSTRUCTION_VTABLE:
    case DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL:
    case DEMANGLE_COMPONENT_PTRMEM_TYPE:
    case DEMANGLE_COMPONENT_UNARY:
    case DEMANGLE_COMPONENT_BINARY:
    case DEMANGLE_COMPONENT_BINARY_ARGS:
    case DEMANGLE_COMPONENT_TRINARY:
    case DEMANGLE_COMPONENT_TRINARY_ARG1:
    case DEMANGLE_COMPONENT_TRINARY_ARG2:
    case DEMANGLE_COMPONENT_LITERAL:
    case DEMANGLE_COMPONENT_LITERAL_NEG:
      if (left == NULL || right == NULL)
	return NULL;
      break;

      /* These types only require one parameter.  */
    case DEMANGLE_COMPONENT_VTABLE:
    case DEMANGLE_COMPONENT_VTT:
    case DEMANGLE_COMPONENT_TYPEINFO:
    case DEMANGLE_COMPONENT_TYPEINFO_NAME:
    case DEMANGLE_COMPONENT_TYPEINFO_FN:
    case DEMANGLE_COMPONENT_THUNK:
    case DEMANGLE_COMPONENT_VIRTUAL_THUNK:
    case DEMANGLE_COMPONENT_COVARIANT_THUNK:
    case DEMANGLE_COMPONENT_JAVA_CLASS:
    case DEMANGLE_COMPONENT_GUARD:
    case DEMANGLE_COMPONENT_REFTEMP:
    case DEMANGLE_COMPONENT_POINTER:
    case DEMANGLE_COMPONENT_REFERENCE:
    case DEMANGLE_COMPONENT_COMPLEX:
    case DEMANGLE_COMPONENT_IMAGINARY:
    case DEMANGLE_COMPONENT_VENDOR_TYPE:
    case DEMANGLE_COMPONENT_ARGLIST:
    case DEMANGLE_COMPONENT_TEMPLATE_ARGLIST:
    case DEMANGLE_COMPONENT_CAST:
      if (left == NULL)
	return NULL;
      break;

      /* This needs a right parameter, but the left parameter can be
	 empty.  */
    case DEMANGLE_COMPONENT_ARRAY_TYPE:
      if (right == NULL)
	return NULL;
      break;

      /* These are allowed to have no parameters--in some cases they
	 will be filled in later.  */
    case DEMANGLE_COMPONENT_FUNCTION_TYPE:
    case DEMANGLE_COMPONENT_RESTRICT:
    case DEMANGLE_COMPONENT_VOLATILE:
    case DEMANGLE_COMPONENT_CONST:
    case DEMANGLE_COMPONENT_RESTRICT_THIS:
    case DEMANGLE_COMPONENT_VOLATILE_THIS:
    case DEMANGLE_COMPONENT_CONST_THIS:
      break;

      /* Other types should not be seen here.  */
    default:
      return NULL;
    }

  p = d_make_empty (di);
  if (p != NULL)
    {
      p->type = type;
      p->u.s_binary.left = left;
      p->u.s_binary.right = right;
    }
  return p;
}

/* Add a new name component.  */

static struct demangle_component *
d_make_name (di, s, len)
     struct d_info *di;
     const char *s;
     int len;
{
  struct demangle_component *p;

  p = d_make_empty (di);
  if (! cplus_demangle_fill_name (p, s, len))
    return NULL;
  return p;
}

/* Add a new builtin type component.  */

static struct demangle_component *
d_make_builtin_type (di, type)
     struct d_info *di;
     const struct demangle_builtin_type_info *type;
{
  struct demangle_component *p;

  if (type == NULL)
    return NULL;
  p = d_make_empty (di);
  if (p != NULL)
    {
      p->type = DEMANGLE_COMPONENT_BUILTIN_TYPE;
      p->u.s_builtin.type = type;
    }
  return p;
}

/* Add a new operator component.  */

static struct demangle_component *
d_make_operator (di, op)
     struct d_info *di;
     const struct demangle_operator_info *op;
{
  struct demangle_component *p;

  p = d_make_empty (di);
  if (p != NULL)
    {
      p->type = DEMANGLE_COMPONENT_OPERATOR;
      p->u.s_operator.op = op;
    }
  return p;
}

/* Add a new extended operator component.  */

static struct demangle_component *
d_make_extended_operator (di, args, name)
     struct d_info *di;
     int args;
     struct demangle_component *name;
{
  struct demangle_component *p;

  p = d_make_empty (di);
  if (! cplus_demangle_fill_extended_operator (p, args, name))
    return NULL;
  return p;
}

/* Add a new constructor component.  */

static struct demangle_component *
d_make_ctor (di, kind,  name)
     struct d_info *di;
     enum gnu_v3_ctor_kinds kind;
     struct demangle_component *name;
{
  struct demangle_component *p;

  p = d_make_empty (di);
  if (! cplus_demangle_fill_ctor (p, kind, name))
    return NULL;
  return p;
}

/* Add a new destructor component.  */

static struct demangle_component *
d_make_dtor (di, kind, name)
     struct d_info *di;
     enum gnu_v3_dtor_kinds kind;
     struct demangle_component *name;
{
  struct demangle_component *p;

  p = d_make_empty (di);
  if (! cplus_demangle_fill_dtor (p, kind, name))
    return NULL;
  return p;
}

/* Add a new template parameter.  */

static struct demangle_component *
d_make_template_param (di, i)
     struct d_info *di;
     long i;
{
  struct demangle_component *p;

  p = d_make_empty (di);
  if (p != NULL)
    {
      p->type = DEMANGLE_COMPONENT_TEMPLATE_PARAM;
      p->u.s_number.number = i;
    }
  return p;
}

/* Add a new standard substitution component.  */

static struct demangle_component *
d_make_sub (di, name, len)
     struct d_info *di;
     const char *name;
     int len;
{
  struct demangle_component *p;

  p = d_make_empty (di);
  if (p != NULL)
    {
      p->type = DEMANGLE_COMPONENT_SUB_STD;
      p->u.s_string.string = name;
      p->u.s_string.len = len;
    }
  return p;
}

/* <mangled-name> ::= _Z <encoding>

   TOP_LEVEL is non-zero when called at the top level.  */

CP_STATIC_IF_GLIBCPP_V3
struct demangle_component *
cplus_demangle_mangled_name (di, top_level)
     struct d_info *di;
     int top_level;
{
  if (d_next_char (di) != '_')
    return NULL;
  if (d_next_char (di) != 'Z')
    return NULL;
  return d_encoding (di, top_level);
}

/* Return whether a function should have a return type.  The argument
   is the function name, which may be qualified in various ways.  The
   rules are that template functions have return types with some
   exceptions, function types which are not part of a function name
   mangling have return types with some exceptions, and non-template
   function names do not have return types.  The exceptions are that
   constructors, destructors, and conversion operators do not have
   return types.  */

static int
has_return_type (dc)
     struct demangle_component *dc;
{
  if (dc == NULL)
    return 0;
  switch (dc->type)
    {
    default:
      return 0;
    case DEMANGLE_COMPONENT_TEMPLATE:
      return ! is_ctor_dtor_or_conversion (d_left (dc));
    case DEMANGLE_COMPONENT_RESTRICT_THIS:
    case DEMANGLE_COMPONENT_VOLATILE_THIS:
    case DEMANGLE_COMPONENT_CONST_THIS:
      return has_return_type (d_left (dc));
    }
}

/* Return whether a name is a constructor, a destructor, or a
   conversion operator.  */

static int
is_ctor_dtor_or_conversion (dc)
     struct demangle_component *dc;
{
  if (dc == NULL)
    return 0;
  switch (dc->type)
    {
    default:
      return 0;
    case DEMANGLE_COMPONENT_QUAL_NAME:
    case DEMANGLE_COMPONENT_LOCAL_NAME:
      return is_ctor_dtor_or_conversion (d_right (dc));
    case DEMANGLE_COMPONENT_CTOR:
    case DEMANGLE_COMPONENT_DTOR:
    case DEMANGLE_COMPONENT_CAST:
      return 1;
    }
}

/* <encoding> ::= <(function) name> <bare-function-type>
              ::= <(data) name>
              ::= <special-name>

   TOP_LEVEL is non-zero when called at the top level, in which case
   if DMGL_PARAMS is not set we do not demangle the function
   parameters.  We only set this at the top level, because otherwise
   we would not correctly demangle names in local scopes.  */

static struct demangle_component *
d_encoding (di, top_level)
     struct d_info *di;
     int top_level;
{
  char peek = d_peek_char (di);

  if (peek == 'G' || peek == 'T')
    return d_special_name (di);
  else
    {
      struct demangle_component *dc;

      dc = d_name (di);

      if (dc != NULL && top_level && (di->options & DMGL_PARAMS) == 0)
	{
	  /* Strip off any initial CV-qualifiers, as they really apply
	     to the `this' parameter, and they were not output by the
	     v2 demangler without DMGL_PARAMS.  */
	  while (dc->type == DEMANGLE_COMPONENT_RESTRICT_THIS
		 || dc->type == DEMANGLE_COMPONENT_VOLATILE_THIS
		 || dc->type == DEMANGLE_COMPONENT_CONST_THIS)
	    dc = d_left (dc);

	  /* If the top level is a DEMANGLE_COMPONENT_LOCAL_NAME, then
	     there may be CV-qualifiers on its right argument which
	     really apply here; this happens when parsing a class
	     which is local to a function.  */
	  if (dc->type == DEMANGLE_COMPONENT_LOCAL_NAME)
	    {
	      struct demangle_component *dcr;

	      dcr = d_right (dc);
	      while (dcr->type == DEMANGLE_COMPONENT_RESTRICT_THIS
		     || dcr->type == DEMANGLE_COMPONENT_VOLATILE_THIS
		     || dcr->type == DEMANGLE_COMPONENT_CONST_THIS)
		dcr = d_left (dcr);
	      dc->u.s_binary.right = dcr;
	    }

	  return dc;
	}

      peek = d_peek_char (di);
      if (peek == '\0' || peek == 'E')
	return dc;
      return d_make_comp (di, DEMANGLE_COMPONENT_TYPED_NAME, dc,
			  d_bare_function_type (di, has_return_type (dc)));
    }
}

/* <name> ::= <nested-name>
          ::= <unscoped-name>
          ::= <unscoped-template-name> <template-args>
          ::= <local-name>

   <unscoped-name> ::= <unqualified-name>
                   ::= St <unqualified-name>

   <unscoped-template-name> ::= <unscoped-name>
                            ::= <substitution>
*/

static struct demangle_component *
d_name (di)
     struct d_info *di;
{
  char peek = d_peek_char (di);
  struct demangle_component *dc;

  switch (peek)
    {
    case 'N':
      return d_nested_name (di);

    case 'Z':
      return d_local_name (di);

    case 'S':
      {
	int subst;

	if (d_peek_next_char (di) != 't')
	  {
	    dc = d_substitution (di, 0);
	    subst = 1;
	  }
	else
	  {
	    d_advance (di, 2);
	    dc = d_make_comp (di, DEMANGLE_COMPONENT_QUAL_NAME,
			      d_make_name (di, "std", 3),
			      d_unqualified_name (di));
	    di->expansion += 3;
	    subst = 0;
	  }

	if (d_peek_char (di) != 'I')
	  {
	    /* The grammar does not permit this case to occur if we
	       called d_substitution() above (i.e., subst == 1).  We
	       don't bother to check.  */
	  }
	else
	  {
	    /* This is <template-args>, which means that we just saw
	       <unscoped-template-name>, which is a substitution
	       candidate if we didn't just get it from a
	       substitution.  */
	    if (! subst)
	      {
		if (! d_add_substitution (di, dc))
		  return NULL;
	      }
	    dc = d_make_comp (di, DEMANGLE_COMPONENT_TEMPLATE, dc,
			      d_template_args (di));
	  }

	return dc;
      }

    default:
      dc = d_unqualified_name (di);
      if (d_peek_char (di) == 'I')
	{
	  /* This is <template-args>, which means that we just saw
	     <unscoped-template-name>, which is a substitution
	     candidate.  */
	  if (! d_add_substitution (di, dc))
	    return NULL;
	  dc = d_make_comp (di, DEMANGLE_COMPONENT_TEMPLATE, dc,
			    d_template_args (di));
	}
      return dc;
    }
}

/* <nested-name> ::= N [<CV-qualifiers>] <prefix> <unqualified-name> E
                 ::= N [<CV-qualifiers>] <template-prefix> <template-args> E
*/

static struct demangle_component *
d_nested_name (di)
     struct d_info *di;
{
  struct demangle_component *ret;
  struct demangle_component **pret;

  if (d_next_char (di) != 'N')
    return NULL;

  pret = d_cv_qualifiers (di, &ret, 1);
  if (pret == NULL)
    return NULL;

  *pret = d_prefix (di);
  if (*pret == NULL)
    return NULL;

  if (d_next_char (di) != 'E')
    return NULL;

  return ret;
}

/* <prefix> ::= <prefix> <unqualified-name>
            ::= <template-prefix> <template-args>
            ::= <template-param>
            ::=
            ::= <substitution>

   <template-prefix> ::= <prefix> <(template) unqualified-name>
                     ::= <template-param>
                     ::= <substitution>
*/

static struct demangle_component *
d_prefix (di)
     struct d_info *di;
{
  struct demangle_component *ret = NULL;

  while (1)
    {
      char peek;
      enum demangle_component_type comb_type;
      struct demangle_component *dc;

      peek = d_peek_char (di);
      if (peek == '\0')
	return NULL;

      /* The older code accepts a <local-name> here, but I don't see
	 that in the grammar.  The older code does not accept a
	 <template-param> here.  */

      comb_type = DEMANGLE_COMPONENT_QUAL_NAME;
      if (IS_DIGIT (peek)
	  || IS_LOWER (peek)
	  || peek == 'C'
	  || peek == 'D')
	dc = d_unqualified_name (di);
      else if (peek == 'S')
	dc = d_substitution (di, 1);
      else if (peek == 'I')
	{
	  if (ret == NULL)
	    return NULL;
	  comb_type = DEMANGLE_COMPONENT_TEMPLATE;
	  dc = d_template_args (di);
	}
      else if (peek == 'T')
	dc = d_template_param (di);
      else if (peek == 'E')
	return ret;
      else
	return NULL;

      if (ret == NULL)
	ret = dc;
      else
	ret = d_make_comp (di, comb_type, ret, dc);

      if (peek != 'S' && d_peek_char (di) != 'E')
	{
	  if (! d_add_substitution (di, ret))
	    return NULL;
	}
    }
}

/* <unqualified-name> ::= <operator-name>
                      ::= <ctor-dtor-name>
                      ::= <source-name>
*/

static struct demangle_component *
d_unqualified_name (di)
     struct d_info *di;
{
  char peek;

  peek = d_peek_char (di);
  if (IS_DIGIT (peek))
    return d_source_name (di);
  else if (IS_LOWER (peek))
    {
      struct demangle_component *ret;

      ret = d_operator_name (di);
      if (ret != NULL && ret->type == DEMANGLE_COMPONENT_OPERATOR)
	di->expansion += sizeof "operator" + ret->u.s_operator.op->len - 2;
      return ret;
    }
  else if (peek == 'C' || peek == 'D')
    return d_ctor_dtor_name (di);
  else
    return NULL;
}

/* <source-name> ::= <(positive length) number> <identifier>  */

static struct demangle_component *
d_source_name (di)
     struct d_info *di;
{
  long len;
  struct demangle_component *ret;

  len = d_number (di);
  if (len <= 0)
    return NULL;
  ret = d_identifier (di, len);
  di->last_name = ret;
  return ret;
}

/* number ::= [n] <(non-negative decimal integer)>  */

static long
d_number (di)
     struct d_info *di;
{
  int negative;
  char peek;
  long ret;

  negative = 0;
  peek = d_peek_char (di);
  if (peek == 'n')
    {
      negative = 1;
      d_advance (di, 1);
      peek = d_peek_char (di);
    }

  ret = 0;
  while (1)
    {
      if (! IS_DIGIT (peek))
	{
	  if (negative)
	    ret = - ret;
	  return ret;
	}
      ret = ret * 10 + peek - '0';
      d_advance (di, 1);
      peek = d_peek_char (di);
    }
}

/* identifier ::= <(unqualified source code identifier)>  */

static struct demangle_component *
d_identifier (di, len)
     struct d_info *di;
     int len;
{
  const char *name;

  name = d_str (di);

  if (di->send - name < len)
    return NULL;

  d_advance (di, len);

  /* A Java mangled name may have a trailing '$' if it is a C++
     keyword.  This '$' is not included in the length count.  We just
     ignore the '$'.  */
  if ((di->options & DMGL_JAVA) != 0
      && d_peek_char (di) == '$')
    d_advance (di, 1);

  /* Look for something which looks like a gcc encoding of an
     anonymous namespace, and replace it with a more user friendly
     name.  */
  if (len >= (int) ANONYMOUS_NAMESPACE_PREFIX_LEN + 2
      && memcmp (name, ANONYMOUS_NAMESPACE_PREFIX,
		 ANONYMOUS_NAMESPACE_PREFIX_LEN) == 0)
    {
      const char *s;

      s = name + ANONYMOUS_NAMESPACE_PREFIX_LEN;
      if ((*s == '.' || *s == '_' || *s == '$')
	  && s[1] == 'N')
	{
	  di->expansion -= len - sizeof "(anonymous namespace)";
	  return d_make_name (di, "(anonymous namespace)",
			      sizeof "(anonymous namespace)" - 1);
	}
    }

  return d_make_name (di, name, len);
}

/* operator_name ::= many different two character encodings.
                 ::= cv <type>
                 ::= v <digit> <source-name>
*/

#define NL(s) s, (sizeof s) - 1

CP_STATIC_IF_GLIBCPP_V3
const struct demangle_operator_info cplus_demangle_operators[] =
{
  { "aN", NL ("&="),        2 },
  { "aS", NL ("="),         2 },
  { "aa", NL ("&&"),        2 },
  { "ad", NL ("&"),         1 },
  { "an", NL ("&"),         2 },
  { "cl", NL ("()"),        0 },
  { "cm", NL (","),         2 },
  { "co", NL ("~"),         1 },
  { "dV", NL ("/="),        2 },
  { "da", NL ("delete[]"),  1 },
  { "de", NL ("*"),         1 },
  { "dl", NL ("delete"),    1 },
  { "dv", NL ("/"),         2 },
  { "eO", NL ("^="),        2 },
  { "eo", NL ("^"),         2 },
  { "eq", NL ("=="),        2 },
  { "ge", NL (">="),        2 },
  { "gt", NL (">"),         2 },
  { "ix", NL ("[]"),        2 },
  { "lS", NL ("<<="),       2 },
  { "le", NL ("<="),        2 },
  { "ls", NL ("<<"),        2 },
  { "lt", NL ("<"),         2 },
  { "mI", NL ("-="),        2 },
  { "mL", NL ("*="),        2 },
  { "mi", NL ("-"),         2 },
  { "ml", NL ("*"),         2 },
  { "mm", NL ("--"),        1 },
  { "na", NL ("new[]"),     1 },
  { "ne", NL ("!="),        2 },
  { "ng", NL ("-"),         1 },
  { "nt", NL ("!"),         1 },
  { "nw", NL ("new"),       1 },
  { "oR", NL ("|="),        2 },
  { "oo", NL ("||"),        2 },
  { "or", NL ("|"),         2 },
  { "pL", NL ("+="),        2 },
  { "pl", NL ("+"),         2 },
  { "pm", NL ("->*"),       2 },
  { "pp", NL ("++"),        1 },
  { "ps", NL ("+"),         1 },
  { "pt", NL ("->"),        2 },
  { "qu", NL ("?"),         3 },
  { "rM", NL ("%="),        2 },
  { "rS", NL (">>="),       2 },
  { "rm", NL ("%"),         2 },
  { "rs", NL (">>"),        2 },
  { "st", NL ("sizeof "),   1 },
  { "sz", NL ("sizeof "),   1 },
  { NULL, NULL, 0,          0 }
};

static struct demangle_component *
d_operator_name (di)
     struct d_info *di;
{
  char c1;
  char c2;

  c1 = d_next_char (di);
  c2 = d_next_char (di);
  if (c1 == 'v' && IS_DIGIT (c2))
    return d_make_extended_operator (di, c2 - '0', d_source_name (di));
  else if (c1 == 'c' && c2 == 'v')
    return d_make_comp (di, DEMANGLE_COMPONENT_CAST,
			cplus_demangle_type (di), NULL);
  else
    {
      /* LOW is the inclusive lower bound.  */
      int low = 0;
      /* HIGH is the exclusive upper bound.  We subtract one to ignore
	 the sentinel at the end of the array.  */
      int high = ((sizeof (cplus_demangle_operators)
		   / sizeof (cplus_demangle_operators[0]))
		  - 1);

      while (1)
	{
	  int i;
	  const struct demangle_operator_info *p;

	  i = low + (high - low) / 2;
	  p = cplus_demangle_operators + i;

	  if (c1 == p->code[0] && c2 == p->code[1])
	    return d_make_operator (di, p);

	  if (c1 < p->code[0] || (c1 == p->code[0] && c2 < p->code[1]))
	    high = i;
	  else
	    low = i + 1;
	  if (low == high)
	    return NULL;
	}
    }
}

/* <special-name> ::= TV <type>
                  ::= TT <type>
                  ::= TI <type>
                  ::= TS <type>
                  ::= GV <(object) name>
                  ::= T <call-offset> <(base) encoding>
                  ::= Tc <call-offset> <call-offset> <(base) encoding>
   Also g++ extensions:
                  ::= TC <type> <(offset) number> _ <(base) type>
                  ::= TF <type>
                  ::= TJ <type>
                  ::= GR <name>
*/

static struct demangle_component *
d_special_name (di)
     struct d_info *di;
{
  char c;

  di->expansion += 20;
  c = d_next_char (di);
  if (c == 'T')
    {
      switch (d_next_char (di))
	{
	case 'V':
	  di->expansion -= 5;
	  return d_make_comp (di, DEMANGLE_COMPONENT_VTABLE,
			      cplus_demangle_type (di), NULL);
	case 'T':
	  di->expansion -= 10;
	  return d_make_comp (di, DEMANGLE_COMPONENT_VTT,
			      cplus_demangle_type (di), NULL);
	case 'I':
	  return d_make_comp (di, DEMANGLE_COMPONENT_TYPEINFO,
			      cplus_demangle_type (di), NULL);
	case 'S':
	  return d_make_comp (di, DEMANGLE_COMPONENT_TYPEINFO_NAME,
			      cplus_demangle_type (di), NULL);

	case 'h':
	  if (! d_call_offset (di, 'h'))
	    return NULL;
	  return d_make_comp (di, DEMANGLE_COMPONENT_THUNK,
			      d_encoding (di, 0), NULL);

	case 'v':
	  if (! d_call_offset (di, 'v'))
	    return NULL;
	  return d_make_comp (di, DEMANGLE_COMPONENT_VIRTUAL_THUNK,
			      d_encoding (di, 0), NULL);

	case 'c':
	  if (! d_call_offset (di, '\0'))
	    return NULL;
	  if (! d_call_offset (di, '\0'))
	    return NULL;
	  return d_make_comp (di, DEMANGLE_COMPONENT_COVARIANT_THUNK,
			      d_encoding (di, 0), NULL);

	case 'C':
	  {
	    struct demangle_component *derived_type;
	    long offset;
	    struct demangle_component *base_type;

	    derived_type = cplus_demangle_type (di);
	    offset = d_number (di);
	    if (offset < 0)
	      return NULL;
	    if (d_next_char (di) != '_')
	      return NULL;
	    base_type = cplus_demangle_type (di);
	    /* We don't display the offset.  FIXME: We should display
	       it in verbose mode.  */
	    di->expansion += 5;
	    return d_make_comp (di, DEMANGLE_COMPONENT_CONSTRUCTION_VTABLE,
				base_type, derived_type);
	  }

	case 'F':
	  return d_make_comp (di, DEMANGLE_COMPONENT_TYPEINFO_FN,
			      cplus_demangle_type (di), NULL);
	case 'J':
	  return d_make_comp (di, DEMANGLE_COMPONENT_JAVA_CLASS,
			      cplus_demangle_type (di), NULL);

	default:
	  return NULL;
	}
    }
  else if (c == 'G')
    {
      switch (d_next_char (di))
	{
	case 'V':
	  return d_make_comp (di, DEMANGLE_COMPONENT_GUARD, d_name (di), NULL);

	case 'R':
	  return d_make_comp (di, DEMANGLE_COMPONENT_REFTEMP, d_name (di),
			      NULL);

	default:
	  return NULL;
	}
    }
  else
    return NULL;
}

/* <call-offset> ::= h <nv-offset> _
                 ::= v <v-offset> _

   <nv-offset> ::= <(offset) number>

   <v-offset> ::= <(offset) number> _ <(virtual offset) number>

   The C parameter, if not '\0', is a character we just read which is
   the start of the <call-offset>.

   We don't display the offset information anywhere.  FIXME: We should
   display it in verbose mode.  */

static int
d_call_offset (di, c)
     struct d_info *di;
     int c;
{
  if (c == '\0')
    c = d_next_char (di);

  if (c == 'h')
    d_number (di);
  else if (c == 'v')
    {
      d_number (di);
      if (d_next_char (di) != '_')
	return 0;
      d_number (di);
    }
  else
    return 0;

  if (d_next_char (di) != '_')
    return 0;

  return 1;
}

/* <ctor-dtor-name> ::= C1
                    ::= C2
                    ::= C3
                    ::= D0
                    ::= D1
                    ::= D2
*/

static struct demangle_component *
d_ctor_dtor_name (di)
     struct d_info *di;
{
  if (di->last_name != NULL)
    {
      if (di->last_name->type == DEMANGLE_COMPONENT_NAME)
	di->expansion += di->last_name->u.s_name.len;
      else if (di->last_name->type == DEMANGLE_COMPONENT_SUB_STD)
	di->expansion += di->last_name->u.s_string.len;
    }
  switch (d_next_char (di))
    {
    case 'C':
      {
	enum gnu_v3_ctor_kinds kind;

	switch (d_next_char (di))
	  {
	  case '1':
	    kind = gnu_v3_complete_object_ctor;
	    break;
	  case '2':
	    kind = gnu_v3_base_object_ctor;
	    break;
	  case '3':
	    kind = gnu_v3_complete_object_allocating_ctor;
	    break;
	  default:
	    return NULL;
	  }
	return d_make_ctor (di, kind, di->last_name);
      }

    case 'D':
      {
	enum gnu_v3_dtor_kinds kind;

	switch (d_next_char (di))
	  {
	  case '0':
	    kind = gnu_v3_deleting_dtor;
	    break;
	  case '1':
	    kind = gnu_v3_complete_object_dtor;
	    break;
	  case '2':
	    kind = gnu_v3_base_object_dtor;
	    break;
	  default:
	    return NULL;
	  }
	return d_make_dtor (di, kind, di->last_name);
      }

    default:
      return NULL;
    }
}

/* <type> ::= <builtin-type>
          ::= <function-type>
          ::= <class-enum-type>
          ::= <array-type>
          ::= <pointer-to-member-type>
          ::= <template-param>
          ::= <template-template-param> <template-args>
          ::= <substitution>
          ::= <CV-qualifiers> <type>
          ::= P <type>
          ::= R <type>
          ::= C <type>
          ::= G <type>
          ::= U <source-name> <type>

   <builtin-type> ::= various one letter codes
                  ::= u <source-name>
*/

CP_STATIC_IF_GLIBCPP_V3
const struct demangle_builtin_type_info
cplus_demangle_builtin_types[D_BUILTIN_TYPE_COUNT] =
{
  /* a */ { NL ("signed char"),	NL ("signed char"),	D_PRINT_DEFAULT },
  /* b */ { NL ("bool"),	NL ("boolean"),		D_PRINT_BOOL },
  /* c */ { NL ("char"),	NL ("byte"),		D_PRINT_DEFAULT },
  /* d */ { NL ("double"),	NL ("double"),		D_PRINT_FLOAT },
  /* e */ { NL ("long double"),	NL ("long double"),	D_PRINT_FLOAT },
  /* f */ { NL ("float"),	NL ("float"),		D_PRINT_FLOAT },
  /* g */ { NL ("__float128"),	NL ("__float128"),	D_PRINT_FLOAT },
  /* h */ { NL ("unsigned char"), NL ("unsigned char"),	D_PRINT_DEFAULT },
  /* i */ { NL ("int"),		NL ("int"),		D_PRINT_INT },
  /* j */ { NL ("unsigned int"), NL ("unsigned"),	D_PRINT_UNSIGNED },
  /* k */ { NULL, 0,		NULL, 0,		D_PRINT_DEFAULT },
  /* l */ { NL ("long"),	NL ("long"),		D_PRINT_LONG },
  /* m */ { NL ("unsigned long"), NL ("unsigned long"),	D_PRINT_UNSIGNED_LONG },
  /* n */ { NL ("__int128"),	NL ("__int128"),	D_PRINT_DEFAULT },
  /* o */ { NL ("unsigned __int128"), NL ("unsigned __int128"),
	    D_PRINT_DEFAULT },
  /* p */ { NULL, 0,		NULL, 0,		D_PRINT_DEFAULT },
  /* q */ { NULL, 0,		NULL, 0,		D_PRINT_DEFAULT },
  /* r */ { NULL, 0,		NULL, 0,		D_PRINT_DEFAULT },
  /* s */ { NL ("short"),	NL ("short"),		D_PRINT_DEFAULT },
  /* t */ { NL ("unsigned short"), NL ("unsigned short"), D_PRINT_DEFAULT },
  /* u */ { NULL, 0,		NULL, 0,		D_PRINT_DEFAULT },
  /* v */ { NL ("void"),	NL ("void"),		D_PRINT_VOID },
  /* w */ { NL ("wchar_t"),	NL ("char"),		D_PRINT_DEFAULT },
  /* x */ { NL ("long long"),	NL ("long"),		D_PRINT_LONG_LONG },
  /* y */ { NL ("unsigned long long"), NL ("unsigned long long"),
	    D_PRINT_UNSIGNED_LONG_LONG },
  /* z */ { NL ("..."),		NL ("..."),		D_PRINT_DEFAULT },
};

CP_STATIC_IF_GLIBCPP_V3
struct demangle_component *
cplus_demangle_type (di)
     struct d_info *di;
{
  char peek;
  struct demangle_component *ret;
  int can_subst;

  /* The ABI specifies that when CV-qualifiers are used, the base type
     is substitutable, and the fully qualified type is substitutable,
     but the base type with a strict subset of the CV-qualifiers is
     not substitutable.  The natural recursive implementation of the
     CV-qualifiers would cause subsets to be substitutable, so instead
     we pull them all off now.

     FIXME: The ABI says that order-insensitive vendor qualifiers
     should be handled in the same way, but we have no way to tell
     which vendor qualifiers are order-insensitive and which are
     order-sensitive.  So we just assume that they are all
     order-sensitive.  g++ 3.4 supports only one vendor qualifier,
     __vector, and it treats it as order-sensitive when mangling
     names.  */

  peek = d_peek_char (di);
  if (peek == 'r' || peek == 'V' || peek == 'K')
    {
      struct demangle_component **pret;

      pret = d_cv_qualifiers (di, &ret, 0);
      if (pret == NULL)
	return NULL;
      *pret = cplus_demangle_type (di);
      if (! d_add_substitution (di, ret))
	return NULL;
      return ret;
    }

  can_subst = 1;

  switch (peek)
    {
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g':
    case 'h': case 'i': case 'j':           case 'l': case 'm': case 'n':
    case 'o':                               case 's': case 't':
    case 'v': case 'w': case 'x': case 'y': case 'z':
      ret = d_make_builtin_type (di,
				 &cplus_demangle_builtin_types[peek - 'a']);
      di->expansion += ret->u.s_builtin.type->len;
      can_subst = 0;
      d_advance (di, 1);
      break;

    case 'u':
      d_advance (di, 1);
      ret = d_make_comp (di, DEMANGLE_COMPONENT_VENDOR_TYPE,
			 d_source_name (di), NULL);
      break;

    case 'F':
      ret = d_function_type (di);
      break;

    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
    case 'N':
    case 'Z':
      ret = d_class_enum_type (di);
      break;

    case 'A':
      ret = d_array_type (di);
      break;

    case 'M':
      ret = d_pointer_to_member_type (di);
      break;

    case 'T':
      ret = d_template_param (di);
      if (d_peek_char (di) == 'I')
	{
	  /* This is <template-template-param> <template-args>.  The
	     <template-template-param> part is a substitution
	     candidate.  */
	  if (! d_add_substitution (di, ret))
	    return NULL;
	  ret = d_make_comp (di, DEMANGLE_COMPONENT_TEMPLATE, ret,
			     d_template_args (di));
	}
      break;

    case 'S':
      /* If this is a special substitution, then it is the start of
	 <class-enum-type>.  */
      {
	char peek_next;

	peek_next = d_peek_next_char (di);
	if (IS_DIGIT (peek_next)
	    || peek_next == '_'
	    || IS_UPPER (peek_next))
	  {
	    ret = d_substitution (di, 0);
	    /* The substituted name may have been a template name and
	       may be followed by tepmlate args.  */
	    if (d_peek_char (di) == 'I')
	      ret = d_make_comp (di, DEMANGLE_COMPONENT_TEMPLATE, ret,
				 d_template_args (di));
	    else
	      can_subst = 0;
	  }
	else
	  {
	    ret = d_class_enum_type (di);
	    /* If the substitution was a complete type, then it is not
	       a new substitution candidate.  However, if the
	       substitution was followed by template arguments, then
	       the whole thing is a substitution candidate.  */
	    if (ret != NULL && ret->type == DEMANGLE_COMPONENT_SUB_STD)
	      can_subst = 0;
	  }
      }
      break;

    case 'P':
      d_advance (di, 1);
      ret = d_make_comp (di, DEMANGLE_COMPONENT_POINTER,
			 cplus_demangle_type (di), NULL);
      break;

    case 'R':
      d_advance (di, 1);
      ret = d_make_comp (di, DEMANGLE_COMPONENT_REFERENCE,
			 cplus_demangle_type (di), NULL);
      break;

    case 'C':
      d_advance (di, 1);
      ret = d_make_comp (di, DEMANGLE_COMPONENT_COMPLEX,
			 cplus_demangle_type (di), NULL);
      break;

    case 'G':
      d_advance (di, 1);
      ret = d_make_comp (di, DEMANGLE_COMPONENT_IMAGINARY,
			 cplus_demangle_type (di), NULL);
      break;

    case 'U':
      d_advance (di, 1);
      ret = d_source_name (di);
      ret = d_make_comp (di, DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL,
			 cplus_demangle_type (di), ret);
      break;

    default:
      return NULL;
    }

  if (can_subst)
    {
      if (! d_add_substitution (di, ret))
	return NULL;
    }

  return ret;
}

/* <CV-qualifiers> ::= [r] [V] [K]  */

static struct demangle_component **
d_cv_qualifiers (di, pret, member_fn)
     struct d_info *di;
     struct demangle_component **pret;
     int member_fn;
{
  char peek;

  peek = d_peek_char (di);
  while (peek == 'r' || peek == 'V' || peek == 'K')
    {
      enum demangle_component_type t;

      d_advance (di, 1);
      if (peek == 'r')
	{
	  t = (member_fn
	       ? DEMANGLE_COMPONENT_RESTRICT_THIS
	       : DEMANGLE_COMPONENT_RESTRICT);
	  di->expansion += sizeof "restrict";
	}
      else if (peek == 'V')
	{
	  t = (member_fn
	       ? DEMANGLE_COMPONENT_VOLATILE_THIS
	       : DEMANGLE_COMPONENT_VOLATILE);
	  di->expansion += sizeof "volatile";
	}
      else
	{
	  t = (member_fn
	       ? DEMANGLE_COMPONENT_CONST_THIS
	       : DEMANGLE_COMPONENT_CONST);
	  di->expansion += sizeof "const";
	}

      *pret = d_make_comp (di, t, NULL, NULL);
      if (*pret == NULL)
	return NULL;
      pret = &d_left (*pret);

      peek = d_peek_char (di);
    }

  return pret;
}

/* <function-type> ::= F [Y] <bare-function-type> E  */

static struct demangle_component *
d_function_type (di)
     struct d_info *di;
{
  struct demangle_component *ret;

  if (d_next_char (di) != 'F')
    return NULL;
  if (d_peek_char (di) == 'Y')
    {
      /* Function has C linkage.  We don't print this information.
	 FIXME: We should print it in verbose mode.  */
      d_advance (di, 1);
    }
  ret = d_bare_function_type (di, 1);
  if (d_next_char (di) != 'E')
    return NULL;
  return ret;
}

/* <bare-function-type> ::= <type>+  */

static struct demangle_component *
d_bare_function_type (di, has_return_type)
     struct d_info *di;
     int has_return_type;
{
  struct demangle_component *return_type;
  struct demangle_component *tl;
  struct demangle_component **ptl;

  return_type = NULL;
  tl = NULL;
  ptl = &tl;
  while (1)
    {
      char peek;
      struct demangle_component *type;

      peek = d_peek_char (di);
      if (peek == '\0' || peek == 'E')
	break;
      type = cplus_demangle_type (di);
      if (type == NULL)
	return NULL;
      if (has_return_type)
	{
	  return_type = type;
	  has_return_type = 0;
	}
      else
	{
	  *ptl = d_make_comp (di, DEMANGLE_COMPONENT_ARGLIST, type, NULL);
	  if (*ptl == NULL)
	    return NULL;
	  ptl = &d_right (*ptl);
	}
    }

  /* There should be at least one parameter type besides the optional
     return type.  A function which takes no arguments will have a
     single parameter type void.  */
  if (tl == NULL)
    return NULL;

  /* If we have a single parameter type void, omit it.  */
  if (d_right (tl) == NULL
      && d_left (tl)->type == DEMANGLE_COMPONENT_BUILTIN_TYPE
      && d_left (tl)->u.s_builtin.type->print == D_PRINT_VOID)
    {
      di->expansion -= d_left (tl)->u.s_builtin.type->len;
      tl = NULL;
    }

  return d_make_comp (di, DEMANGLE_COMPONENT_FUNCTION_TYPE, return_type, tl);
}

/* <class-enum-type> ::= <name>  */

static struct demangle_component *
d_class_enum_type (di)
     struct d_info *di;
{
  return d_name (di);
}

/* <array-type> ::= A <(positive dimension) number> _ <(element) type>
                ::= A [<(dimension) expression>] _ <(element) type>
*/

static struct demangle_component *
d_array_type (di)
     struct d_info *di;
{
  char peek;
  struct demangle_component *dim;

  if (d_next_char (di) != 'A')
    return NULL;

  peek = d_peek_char (di);
  if (peek == '_')
    dim = NULL;
  else if (IS_DIGIT (peek))
    {
      const char *s;

      s = d_str (di);
      do
	{
	  d_advance (di, 1);
	  peek = d_peek_char (di);
	}
      while (IS_DIGIT (peek));
      dim = d_make_name (di, s, d_str (di) - s);
      if (dim == NULL)
	return NULL;
    }
  else
    {
      dim = d_expression (di);
      if (dim == NULL)
	return NULL;
    }

  if (d_next_char (di) != '_')
    return NULL;

  return d_make_comp (di, DEMANGLE_COMPONENT_ARRAY_TYPE, dim,
		      cplus_demangle_type (di));
}

/* <pointer-to-member-type> ::= M <(class) type> <(member) type>  */

static struct demangle_component *
d_pointer_to_member_type (di)
     struct d_info *di;
{
  struct demangle_component *cl;
  struct demangle_component *mem;
  struct demangle_component **pmem;

  if (d_next_char (di) != 'M')
    return NULL;

  cl = cplus_demangle_type (di);

  /* The ABI specifies that any type can be a substitution source, and
     that M is followed by two types, and that when a CV-qualified
     type is seen both the base type and the CV-qualified types are
     substitution sources.  The ABI also specifies that for a pointer
     to a CV-qualified member function, the qualifiers are attached to
     the second type.  Given the grammar, a plain reading of the ABI
     suggests that both the CV-qualified member function and the
     non-qualified member function are substitution sources.  However,
     g++ does not work that way.  g++ treats only the CV-qualified
     member function as a substitution source.  FIXME.  So to work
     with g++, we need to pull off the CV-qualifiers here, in order to
     avoid calling add_substitution() in cplus_demangle_type().  */

  pmem = d_cv_qualifiers (di, &mem, 1);
  if (pmem == NULL)
    return NULL;
  *pmem = cplus_demangle_type (di);

  return d_make_comp (di, DEMANGLE_COMPONENT_PTRMEM_TYPE, cl, mem);
}

/* <template-param> ::= T_
                    ::= T <(parameter-2 non-negative) number> _
*/

static struct demangle_component *
d_template_param (di)
     struct d_info *di;
{
  long param;

  if (d_next_char (di) != 'T')
    return NULL;

  if (d_peek_char (di) == '_')
    param = 0;
  else
    {
      param = d_number (di);
      if (param < 0)
	return NULL;
      param += 1;
    }

  if (d_next_char (di) != '_')
    return NULL;

  ++di->did_subs;

  return d_make_template_param (di, param);
}

/* <template-args> ::= I <template-arg>+ E  */

static struct demangle_component *
d_template_args (di)
     struct d_info *di;
{
  struct demangle_component *hold_last_name;
  struct demangle_component *al;
  struct demangle_component **pal;

  /* Preserve the last name we saw--don't let the template arguments
     clobber it, as that would give us the wrong name for a subsequent
     constructor or destructor.  */
  hold_last_name = di->last_name;

  if (d_next_char (di) != 'I')
    return NULL;

  al = NULL;
  pal = &al;
  while (1)
    {
      struct demangle_component *a;

      a = d_template_arg (di);
      if (a == NULL)
	return NULL;

      *pal = d_make_comp (di, DEMANGLE_COMPONENT_TEMPLATE_ARGLIST, a, NULL);
      if (*pal == NULL)
	return NULL;
      pal = &d_right (*pal);

      if (d_peek_char (di) == 'E')
	{
	  d_advance (di, 1);
	  break;
	}
    }

  di->last_name = hold_last_name;

  return al;
}

/* <template-arg> ::= <type>
                  ::= X <expression> E
                  ::= <expr-primary>
*/

static struct demangle_component *
d_template_arg (di)
     struct d_info *di;
{
  struct demangle_component *ret;

  switch (d_peek_char (di))
    {
    case 'X':
      d_advance (di, 1);
      ret = d_expression (di);
      if (d_next_char (di) != 'E')
	return NULL;
      return ret;

    case 'L':
      return d_expr_primary (di);

    default:
      return cplus_demangle_type (di);
    }
}

/* <expression> ::= <(unary) operator-name> <expression>
                ::= <(binary) operator-name> <expression> <expression>
                ::= <(trinary) operator-name> <expression> <expression> <expression>
                ::= st <type>
                ::= <template-param>
                ::= sr <type> <unqualified-name>
                ::= sr <type> <unqualified-name> <template-args>
                ::= <expr-primary>
*/

static struct demangle_component *
d_expression (di)
     struct d_info *di;
{
  char peek;

  peek = d_peek_char (di);
  if (peek == 'L')
    return d_expr_primary (di);
  else if (peek == 'T')
    return d_template_param (di);
  else if (peek == 's' && d_peek_next_char (di) == 'r')
    {
      struct demangle_component *type;
      struct demangle_component *name;

      d_advance (di, 2);
      type = cplus_demangle_type (di);
      name = d_unqualified_name (di);
      if (d_peek_char (di) != 'I')
	return d_make_comp (di, DEMANGLE_COMPONENT_QUAL_NAME, type, name);
      else
	return d_make_comp (di, DEMANGLE_COMPONENT_QUAL_NAME, type,
			    d_make_comp (di, DEMANGLE_COMPONENT_TEMPLATE, name,
					 d_template_args (di)));
    }
  else
    {
      struct demangle_component *op;
      int args;

      op = d_operator_name (di);
      if (op == NULL)
	return NULL;

      if (op->type == DEMANGLE_COMPONENT_OPERATOR)
	di->expansion += op->u.s_operator.op->len - 2;

      if (op->type == DEMANGLE_COMPONENT_OPERATOR
	  && strcmp (op->u.s_operator.op->code, "st") == 0)
	return d_make_comp (di, DEMANGLE_COMPONENT_UNARY, op,
			    cplus_demangle_type (di));

      switch (op->type)
	{
	default:
	  return NULL;
	case DEMANGLE_COMPONENT_OPERATOR:
	  args = op->u.s_operator.op->args;
	  break;
	case DEMANGLE_COMPONENT_EXTENDED_OPERATOR:
	  args = op->u.s_extended_operator.args;
	  break;
	case DEMANGLE_COMPONENT_CAST:
	  args = 1;
	  break;
	}

      switch (args)
	{
	case 1:
	  return d_make_comp (di, DEMANGLE_COMPONENT_UNARY, op,
			      d_expression (di));
	case 2:
	  {
	    struct demangle_component *left;

	    left = d_expression (di);
	    return d_make_comp (di, DEMANGLE_COMPONENT_BINARY, op,
				d_make_comp (di,
					     DEMANGLE_COMPONENT_BINARY_ARGS,
					     left,
					     d_expression (di)));
	  }
	case 3:
	  {
	    struct demangle_component *first;
	    struct demangle_component *second;

	    first = d_expression (di);
	    second = d_expression (di);
	    return d_make_comp (di, DEMANGLE_COMPONENT_TRINARY, op,
				d_make_comp (di,
					     DEMANGLE_COMPONENT_TRINARY_ARG1,
					     first,
					     d_make_comp (di,
							  DEMANGLE_COMPONENT_TRINARY_ARG2,
							  second,
							  d_expression (di))));
	  }
	default:
	  return NULL;
	}
    }
}

/* <expr-primary> ::= L <type> <(value) number> E
                  ::= L <type> <(value) float> E
                  ::= L <mangled-name> E
*/

static struct demangle_component *
d_expr_primary (di)
     struct d_info *di;
{
  struct demangle_component *ret;

  if (d_next_char (di) != 'L')
    return NULL;
  if (d_peek_char (di) == '_')
    ret = cplus_demangle_mangled_name (di, 0);
  else
    {
      struct demangle_component *type;
      enum demangle_component_type t;
      const char *s;

      type = cplus_demangle_type (di);
      if (type == NULL)
	return NULL;

      /* If we have a type we know how to print, we aren't going to
	 print the type name itself.  */
      if (type->type == DEMANGLE_COMPONENT_BUILTIN_TYPE
	  && type->u.s_builtin.type->print != D_PRINT_DEFAULT)
	di->expansion -= type->u.s_builtin.type->len;

      /* Rather than try to interpret the literal value, we just
	 collect it as a string.  Note that it's possible to have a
	 floating point literal here.  The ABI specifies that the
	 format of such literals is machine independent.  That's fine,
	 but what's not fine is that versions of g++ up to 3.2 with
	 -fabi-version=1 used upper case letters in the hex constant,
	 and dumped out gcc's internal representation.  That makes it
	 hard to tell where the constant ends, and hard to dump the
	 constant in any readable form anyhow.  We don't attempt to
	 handle these cases.  */

      t = DEMANGLE_COMPONENT_LITERAL;
      if (d_peek_char (di) == 'n')
	{
	  t = DEMANGLE_COMPONENT_LITERAL_NEG;
	  d_advance (di, 1);
	}
      s = d_str (di);
      while (d_peek_char (di) != 'E')
	d_advance (di, 1);
      ret = d_make_comp (di, t, type, d_make_name (di, s, d_str (di) - s));
    }
  if (d_next_char (di) != 'E')
    return NULL;
  return ret;
}

/* <local-name> ::= Z <(function) encoding> E <(entity) name> [<discriminator>]
                ::= Z <(function) encoding> E s [<discriminator>]
*/

static struct demangle_component *
d_local_name (di)
     struct d_info *di;
{
  struct demangle_component *function;

  if (d_next_char (di) != 'Z')
    return NULL;

  function = d_encoding (di, 0);

  if (d_next_char (di) != 'E')
    return NULL;

  if (d_peek_char (di) == 's')
    {
      d_advance (di, 1);
      if (! d_discriminator (di))
	return NULL;
      return d_make_comp (di, DEMANGLE_COMPONENT_LOCAL_NAME, function,
			  d_make_name (di, "string literal",
				       sizeof "string literal" - 1));
    }
  else
    {
      struct demangle_component *name;

      name = d_name (di);
      if (! d_discriminator (di))
	return NULL;
      return d_make_comp (di, DEMANGLE_COMPONENT_LOCAL_NAME, function, name);
    }
}

/* <discriminator> ::= _ <(non-negative) number>

   We demangle the discriminator, but we don't print it out.  FIXME:
   We should print it out in verbose mode.  */

static int
d_discriminator (di)
     struct d_info *di;
{
  long discrim;

  if (d_peek_char (di) != '_')
    return 1;
  d_advance (di, 1);
  discrim = d_number (di);
  if (discrim < 0)
    return 0;
  return 1;
}

/* Add a new substitution.  */

static int
d_add_substitution (di, dc)
     struct d_info *di;
     struct demangle_component *dc;
{
  if (dc == NULL)
    return 0;
  if (di->next_sub >= di->num_subs)
    return 0;
  di->subs[di->next_sub] = dc;
  ++di->next_sub;
  return 1;
}

/* <substitution> ::= S <seq-id> _
                  ::= S_
                  ::= St
                  ::= Sa
                  ::= Sb
                  ::= Ss
                  ::= Si
                  ::= So
                  ::= Sd

   If PREFIX is non-zero, then this type is being used as a prefix in
   a qualified name.  In this case, for the standard substitutions, we
   need to check whether we are being used as a prefix for a
   constructor or destructor, and return a full template name.
   Otherwise we will get something like std::iostream::~iostream()
   which does not correspond particularly well to any function which
   actually appears in the source.
*/

static const struct d_standard_sub_info standard_subs[] =
{
  { 't', NL ("std"),
    NL ("std"),
    NULL, 0 },
  { 'a', NL ("std::allocator"),
    NL ("std::allocator"),
    NL ("allocator") },
  { 'b', NL ("std::basic_string"),
    NL ("std::basic_string"),
    NL ("basic_string") },
  { 's', NL ("std::string"),
    NL ("std::basic_string<char, std::char_traits<char>, std::allocator<char> >"),
    NL ("basic_string") },
  { 'i', NL ("std::istream"),
    NL ("std::basic_istream<char, std::char_traits<char> >"),
    NL ("basic_istream") },
  { 'o', NL ("std::ostream"),
    NL ("std::basic_ostream<char, std::char_traits<char> >"),
    NL ("basic_ostream") },
  { 'd', NL ("std::iostream"),
    NL ("std::basic_iostream<char, std::char_traits<char> >"),
    NL ("basic_iostream") }
};

static struct demangle_component *
d_substitution (di, prefix)
     struct d_info *di;
     int prefix;
{
  char c;

  if (d_next_char (di) != 'S')
    return NULL;

  c = d_next_char (di);
  if (c == '_' || IS_DIGIT (c) || IS_UPPER (c))
    {
      int id;

      id = 0;
      if (c != '_')
	{
	  do
	    {
	      if (IS_DIGIT (c))
		id = id * 36 + c - '0';
	      else if (IS_UPPER (c))
		id = id * 36 + c - 'A' + 10;
	      else
		return NULL;
	      c = d_next_char (di);
	    }
	  while (c != '_');

	  ++id;
	}

      if (id >= di->next_sub)
	return NULL;

      ++di->did_subs;

      return di->subs[id];
    }
  else
    {
      int verbose;
      const struct d_standard_sub_info *p;
      const struct d_standard_sub_info *pend;

      verbose = (di->options & DMGL_VERBOSE) != 0;
      if (! verbose && prefix)
	{
	  char peek;

	  peek = d_peek_char (di);
	  if (peek == 'C' || peek == 'D')
	    verbose = 1;
	}

      pend = (&standard_subs[0]
	      + sizeof standard_subs / sizeof standard_subs[0]);
      for (p = &standard_subs[0]; p < pend; ++p)
	{
	  if (c == p->code)
	    {
	      const char *s;
	      int len;

	      if (p->set_last_name != NULL)
		di->last_name = d_make_sub (di, p->set_last_name,
					    p->set_last_name_len);
	      if (verbose)
		{
		  s = p->full_expansion;
		  len = p->full_len;
		}
	      else
		{
		  s = p->simple_expansion;
		  len = p->simple_len;
		}
	      di->expansion += len;
	      return d_make_sub (di, s, len);
	    }
	}

      return NULL;
    }
}

/* Resize the print buffer.  */

static void
d_print_resize (dpi, add)
     struct d_print_info *dpi;
     size_t add;
{
  size_t need;

  if (dpi->buf == NULL)
    return;
  need = dpi->len + add;
  while (need > dpi->alc)
    {
      size_t newalc;
      char *newbuf;

      newalc = dpi->alc * 2;
      newbuf = realloc (dpi->buf, newalc);
      if (newbuf == NULL)
	{
	  free (dpi->buf);
	  dpi->buf = NULL;
	  dpi->allocation_failure = 1;
	  return;
	}
      dpi->buf = newbuf;
      dpi->alc = newalc;
    }
}

/* Append a character to the print buffer.  */

static void
d_print_append_char (dpi, c)
     struct d_print_info *dpi;
     int c;
{
  if (dpi->buf != NULL)
    {
      if (dpi->len >= dpi->alc)
	{
	  d_print_resize (dpi, 1);
	  if (dpi->buf == NULL)
	    return;
	}

      dpi->buf[dpi->len] = c;
      ++dpi->len;
    }
}

/* Append a buffer to the print buffer.  */

static void
d_print_append_buffer (dpi, s, l)
     struct d_print_info *dpi;
     const char *s;
     size_t l;
{
  if (dpi->buf != NULL)
    {
      if (dpi->len + l > dpi->alc)
	{
	  d_print_resize (dpi, l);
	  if (dpi->buf == NULL)
	    return;
	}

      memcpy (dpi->buf + dpi->len, s, l);
      dpi->len += l;
    }
}

/* Indicate that an error occurred during printing.  */

static void
d_print_error (dpi)
     struct d_print_info *dpi;
{
  free (dpi->buf);
  dpi->buf = NULL;
}

/* Turn components into a human readable string.  OPTIONS is the
   options bits passed to the demangler.  DC is the tree to print.
   ESTIMATE is a guess at the length of the result.  This returns a
   string allocated by malloc, or NULL on error.  On success, this
   sets *PALC to the size of the allocated buffer.  On failure, this
   sets *PALC to 0 for a bad parse, or to 1 for a memory allocation
   failure.  */

CP_STATIC_IF_GLIBCPP_V3
char *
cplus_demangle_print (options, dc, estimate, palc)
     int options;
     const struct demangle_component *dc;
     int estimate;
     size_t *palc;
{
  struct d_print_info dpi;

  dpi.options = options;

  dpi.alc = estimate + 1;
  dpi.buf = malloc (dpi.alc);
  if (dpi.buf == NULL)
    {
      *palc = 1;
      return NULL;
    }

  dpi.len = 0;
  dpi.templates = NULL;
  dpi.modifiers = NULL;

  dpi.allocation_failure = 0;

  d_print_comp (&dpi, dc);

  d_append_char (&dpi, '\0');

  if (dpi.buf != NULL)
    *palc = dpi.alc;
  else
    *palc = dpi.allocation_failure;

  return dpi.buf;
}

/* Subroutine to handle components.  */

static void
d_print_comp (dpi, dc)
     struct d_print_info *dpi;
     const struct demangle_component *dc;
{
  if (dc == NULL)
    {
      d_print_error (dpi);
      return;
    }
  if (d_print_saw_error (dpi))
    return;

  switch (dc->type)
    {
    case DEMANGLE_COMPONENT_NAME:
      if ((dpi->options & DMGL_JAVA) == 0)
	d_append_buffer (dpi, dc->u.s_name.s, dc->u.s_name.len);
      else
	d_print_java_identifier (dpi, dc->u.s_name.s, dc->u.s_name.len);
      return;

    case DEMANGLE_COMPONENT_QUAL_NAME:
    case DEMANGLE_COMPONENT_LOCAL_NAME:
      d_print_comp (dpi, d_left (dc));
      if ((dpi->options & DMGL_JAVA) == 0)
	d_append_string_constant (dpi, "::");
      else
	d_append_char (dpi, '.');
      d_print_comp (dpi, d_right (dc));
      return;

    case DEMANGLE_COMPONENT_TYPED_NAME:
      {
	struct d_print_mod *hold_modifiers;
	struct demangle_component *typed_name;
	struct d_print_mod adpm[4];
	unsigned int i;
	struct d_print_template dpt;

	/* Pass the name down to the type so that it can be printed in
	   the right place for the type.  We also have to pass down
	   any CV-qualifiers, which apply to the this parameter.  */
	hold_modifiers = dpi->modifiers;
	i = 0;
	typed_name = d_left (dc);
	while (typed_name != NULL)
	  {
	    if (i >= sizeof adpm / sizeof adpm[0])
	      {
		d_print_error (dpi);
		return;
	      }

	    adpm[i].next = dpi->modifiers;
	    dpi->modifiers = &adpm[i];
	    adpm[i].mod = typed_name;
	    adpm[i].printed = 0;
	    adpm[i].templates = dpi->templates;
	    ++i;

	    if (typed_name->type != DEMANGLE_COMPONENT_RESTRICT_THIS
		&& typed_name->type != DEMANGLE_COMPONENT_VOLATILE_THIS
		&& typed_name->type != DEMANGLE_COMPONENT_CONST_THIS)
	      break;

	    typed_name = d_left (typed_name);
	  }

	/* If typed_name is a template, then it applies to the
	   function type as well.  */
	if (typed_name->type == DEMANGLE_COMPONENT_TEMPLATE)
	  {
	    dpt.next = dpi->templates;
	    dpi->templates = &dpt;
	    dpt.template = typed_name;
	  }

	/* If typed_name is a DEMANGLE_COMPONENT_LOCAL_NAME, then
	   there may be CV-qualifiers on its right argument which
	   really apply here; this happens when parsing a class which
	   is local to a function.  */
	if (typed_name->type == DEMANGLE_COMPONENT_LOCAL_NAME)
	  {
	    struct demangle_component *local_name;

	    local_name = d_right (typed_name);
	    while (local_name->type == DEMANGLE_COMPONENT_RESTRICT_THIS
		   || local_name->type == DEMANGLE_COMPONENT_VOLATILE_THIS
		   || local_name->type == DEMANGLE_COMPONENT_CONST_THIS)
	      {
		if (i >= sizeof adpm / sizeof adpm[0])
		  {
		    d_print_error (dpi);
		    return;
		  }

		adpm[i] = adpm[i - 1];
		adpm[i].next = &adpm[i - 1];
		dpi->modifiers = &adpm[i];

		adpm[i - 1].mod = local_name;
		adpm[i - 1].printed = 0;
		adpm[i - 1].templates = dpi->templates;
		++i;

		local_name = d_left (local_name);
	      }
	  }

	d_print_comp (dpi, d_right (dc));

	if (typed_name->type == DEMANGLE_COMPONENT_TEMPLATE)
	  dpi->templates = dpt.next;

	/* If the modifiers didn't get printed by the type, print them
	   now.  */
	while (i > 0)
	  {
	    --i;
	    if (! adpm[i].printed)
	      {
		d_append_char (dpi, ' ');
		d_print_mod (dpi, adpm[i].mod);
	      }
	  }

	dpi->modifiers = hold_modifiers;

	return;
      }

    case DEMANGLE_COMPONENT_TEMPLATE:
      {
	struct d_print_mod *hold_dpm;

	/* Don't push modifiers into a template definition.  Doing so
	   could give the wrong definition for a template argument.
	   Instead, treat the template essentially as a name.  */

	hold_dpm = dpi->modifiers;
	dpi->modifiers = NULL;

	d_print_comp (dpi, d_left (dc));
	if (d_last_char (dpi) == '<')
	  d_append_char (dpi, ' ');
	d_append_char (dpi, '<');
	d_print_comp (dpi, d_right (dc));
	/* Avoid generating two consecutive '>' characters, to avoid
	   the C++ syntactic ambiguity.  */
	if (d_last_char (dpi) == '>')
	  d_append_char (dpi, ' ');
	d_append_char (dpi, '>');

	dpi->modifiers = hold_dpm;

	return;
      }

    case DEMANGLE_COMPONENT_TEMPLATE_PARAM:
      {
	long i;
	struct demangle_component *a;
	struct d_print_template *hold_dpt;

	if (dpi->templates == NULL)
	  {
	    d_print_error (dpi);
	    return;
	  }
	i = dc->u.s_number.number;
	for (a = d_right (dpi->templates->template);
	     a != NULL;
	     a = d_right (a))
	  {
	    if (a->type != DEMANGLE_COMPONENT_TEMPLATE_ARGLIST)
	      {
		d_print_error (dpi);
		return;
	      }
	    if (i <= 0)
	      break;
	    --i;
	  }
	if (i != 0 || a == NULL)
	  {
	    d_print_error (dpi);
	    return;
	  }

	/* While processing this parameter, we need to pop the list of
	   templates.  This is because the template parameter may
	   itself be a reference to a parameter of an outer
	   template.  */

	hold_dpt = dpi->templates;
	dpi->templates = hold_dpt->next;

	d_print_comp (dpi, d_left (a));

	dpi->templates = hold_dpt;

	return;
      }

    case DEMANGLE_COMPONENT_CTOR:
      d_print_comp (dpi, dc->u.s_ctor.name);
      return;

    case DEMANGLE_COMPONENT_DTOR:
      d_append_char (dpi, '~');
      d_print_comp (dpi, dc->u.s_dtor.name);
      return;

    case DEMANGLE_COMPONENT_VTABLE:
      d_append_string_constant (dpi, "vtable for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_VTT:
      d_append_string_constant (dpi, "VTT for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_CONSTRUCTION_VTABLE:
      d_append_string_constant (dpi, "construction vtable for ");
      d_print_comp (dpi, d_left (dc));
      d_append_string_constant (dpi, "-in-");
      d_print_comp (dpi, d_right (dc));
      return;

    case DEMANGLE_COMPONENT_TYPEINFO:
      d_append_string_constant (dpi, "typeinfo for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_TYPEINFO_NAME:
      d_append_string_constant (dpi, "typeinfo name for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_TYPEINFO_FN:
      d_append_string_constant (dpi, "typeinfo fn for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_THUNK:
      d_append_string_constant (dpi, "non-virtual thunk to ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_VIRTUAL_THUNK:
      d_append_string_constant (dpi, "virtual thunk to ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_COVARIANT_THUNK:
      d_append_string_constant (dpi, "covariant return thunk to ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_JAVA_CLASS:
      d_append_string_constant (dpi, "java Class for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_GUARD:
      d_append_string_constant (dpi, "guard variable for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_REFTEMP:
      d_append_string_constant (dpi, "reference temporary for ");
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_SUB_STD:
      d_append_buffer (dpi, dc->u.s_string.string, dc->u.s_string.len);
      return;

    case DEMANGLE_COMPONENT_RESTRICT:
    case DEMANGLE_COMPONENT_VOLATILE:
    case DEMANGLE_COMPONENT_CONST:
      {
	struct d_print_mod *pdpm;

	/* When printing arrays, it's possible to have cases where the
	   same CV-qualifier gets pushed on the stack multiple times.
	   We only need to print it once.  */

	for (pdpm = dpi->modifiers; pdpm != NULL; pdpm = pdpm->next)
	  {
	    if (! pdpm->printed)
	      {
		if (pdpm->mod->type != DEMANGLE_COMPONENT_RESTRICT
		    && pdpm->mod->type != DEMANGLE_COMPONENT_VOLATILE
		    && pdpm->mod->type != DEMANGLE_COMPONENT_CONST)
		  break;
		if (pdpm->mod->type == dc->type)
		  {
		    d_print_comp (dpi, d_left (dc));
		    return;
		  }
	      }
	  }
      }
      /* Fall through.  */
    case DEMANGLE_COMPONENT_RESTRICT_THIS:
    case DEMANGLE_COMPONENT_VOLATILE_THIS:
    case DEMANGLE_COMPONENT_CONST_THIS:
    case DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL:
    case DEMANGLE_COMPONENT_POINTER:
    case DEMANGLE_COMPONENT_REFERENCE:
    case DEMANGLE_COMPONENT_COMPLEX:
    case DEMANGLE_COMPONENT_IMAGINARY:
      {
	/* We keep a list of modifiers on the stack.  */
	struct d_print_mod dpm;

	dpm.next = dpi->modifiers;
	dpi->modifiers = &dpm;
	dpm.mod = dc;
	dpm.printed = 0;
	dpm.templates = dpi->templates;

	d_print_comp (dpi, d_left (dc));

	/* If the modifier didn't get printed by the type, print it
	   now.  */
	if (! dpm.printed)
	  d_print_mod (dpi, dc);

	dpi->modifiers = dpm.next;

	return;
      }

    case DEMANGLE_COMPONENT_BUILTIN_TYPE:
      if ((dpi->options & DMGL_JAVA) == 0)
	d_append_buffer (dpi, dc->u.s_builtin.type->name,
			 dc->u.s_builtin.type->len);
      else
	d_append_buffer (dpi, dc->u.s_builtin.type->java_name,
			 dc->u.s_builtin.type->java_len);
      return;

    case DEMANGLE_COMPONENT_VENDOR_TYPE:
      d_print_comp (dpi, d_left (dc));
      return;

    case DEMANGLE_COMPONENT_FUNCTION_TYPE:
      {
	if (d_left (dc) != NULL)
	  {
	    struct d_print_mod dpm;

	    /* We must pass this type down as a modifier in order to
	       print it in the right location.  */

	    dpm.next = dpi->modifiers;
	    dpi->modifiers = &dpm;
	    dpm.mod = dc;
	    dpm.printed = 0;
	    dpm.templates = dpi->templates;

	    d_print_comp (dpi, d_left (dc));

	    dpi->modifiers = dpm.next;

	    if (dpm.printed)
	      return;

	    d_append_char (dpi, ' ');
	  }

	d_print_function_type (dpi, dc, dpi->modifiers);

	return;
      }

    case DEMANGLE_COMPONENT_ARRAY_TYPE:
      {
	struct d_print_mod *hold_modifiers;
	struct d_print_mod adpm[4];
	unsigned int i;
	struct d_print_mod *pdpm;

	/* We must pass this type down as a modifier in order to print
	   multi-dimensional arrays correctly.  If the array itself is
	   CV-qualified, we act as though the element type were
	   CV-qualified.  We do this by copying the modifiers down
	   rather than fiddling pointers, so that we don't wind up
	   with a d_print_mod higher on the stack pointing into our
	   stack frame after we return.  */

	hold_modifiers = dpi->modifiers;

	adpm[0].next = hold_modifiers;
	dpi->modifiers = &adpm[0];
	adpm[0].mod = dc;
	adpm[0].printed = 0;
	adpm[0].templates = dpi->templates;

	i = 1;
	pdpm = hold_modifiers;
	while (pdpm != NULL
	       && (pdpm->mod->type == DEMANGLE_COMPONENT_RESTRICT
		   || pdpm->mod->type == DEMANGLE_COMPONENT_VOLATILE
		   || pdpm->mod->type == DEMANGLE_COMPONENT_CONST))
	  {
	    if (! pdpm->printed)
	      {
		if (i >= sizeof adpm / sizeof adpm[0])
		  {
		    d_print_error (dpi);
		    return;
		  }

		adpm[i] = *pdpm;
		adpm[i].next = dpi->modifiers;
		dpi->modifiers = &adpm[i];
		pdpm->printed = 1;
		++i;
	      }

	    pdpm = pdpm->next;
	  }

	d_print_comp (dpi, d_right (dc));

	dpi->modifiers = hold_modifiers;

	if (adpm[0].printed)
	  return;

	while (i > 1)
	  {
	    --i;
	    d_print_mod (dpi, adpm[i].mod);
	  }

	d_print_array_type (dpi, dc, dpi->modifiers);

	return;
      }

    case DEMANGLE_COMPONENT_PTRMEM_TYPE:
      {
	struct d_print_mod dpm;

	dpm.next = dpi->modifiers;
	dpi->modifiers = &dpm;
	dpm.mod = dc;
	dpm.printed = 0;
	dpm.templates = dpi->templates;

	d_print_comp (dpi, d_right (dc));

	/* If the modifier didn't get printed by the type, print it
	   now.  */
	if (! dpm.printed)
	  {
	    d_append_char (dpi, ' ');
	    d_print_comp (dpi, d_left (dc));
	    d_append_string_constant (dpi, "::*");
	  }

	dpi->modifiers = dpm.next;

	return;
      }

    case DEMANGLE_COMPONENT_ARGLIST:
    case DEMANGLE_COMPONENT_TEMPLATE_ARGLIST:
      d_print_comp (dpi, d_left (dc));
      if (d_right (dc) != NULL)
	{
	  d_append_string_constant (dpi, ", ");
	  d_print_comp (dpi, d_right (dc));
	}
      return;

    case DEMANGLE_COMPONENT_OPERATOR:
      {
	char c;

	d_append_string_constant (dpi, "operator");
	c = dc->u.s_operator.op->name[0];
	if (IS_LOWER (c))
	  d_append_char (dpi, ' ');
	d_append_buffer (dpi, dc->u.s_operator.op->name,
			 dc->u.s_operator.op->len);
	return;
      }

    case DEMANGLE_COMPONENT_EXTENDED_OPERATOR:
      d_append_string_constant (dpi, "operator ");
      d_print_comp (dpi, dc->u.s_extended_operator.name);
      return;

    case DEMANGLE_COMPONENT_CAST:
      d_append_string_constant (dpi, "operator ");
      d_print_cast (dpi, dc);
      return;

    case DEMANGLE_COMPONENT_UNARY:
      if (d_left (dc)->type != DEMANGLE_COMPONENT_CAST)
	d_print_expr_op (dpi, d_left (dc));
      else
	{
	  d_append_char (dpi, '(');
	  d_print_cast (dpi, d_left (dc));
	  d_append_char (dpi, ')');
	}
      d_append_char (dpi, '(');
      d_print_comp (dpi, d_right (dc));
      d_append_char (dpi, ')');
      return;

    case DEMANGLE_COMPONENT_BINARY:
      if (d_right (dc)->type != DEMANGLE_COMPONENT_BINARY_ARGS)
	{
	  d_print_error (dpi);
	  return;
	}

      /* We wrap an expression which uses the greater-than operator in
	 an extra layer of parens so that it does not get confused
	 with the '>' which ends the template parameters.  */
      if (d_left (dc)->type == DEMANGLE_COMPONENT_OPERATOR
	  && d_left (dc)->u.s_operator.op->len == 1
	  && d_left (dc)->u.s_operator.op->name[0] == '>')
	d_append_char (dpi, '(');

      d_append_char (dpi, '(');
      d_print_comp (dpi, d_left (d_right (dc)));
      d_append_string_constant (dpi, ") ");
      d_print_expr_op (dpi, d_left (dc));
      d_append_string_constant (dpi, " (");
      d_print_comp (dpi, d_right (d_right (dc)));
      d_append_char (dpi, ')');

      if (d_left (dc)->type == DEMANGLE_COMPONENT_OPERATOR
	  && d_left (dc)->u.s_operator.op->len == 1
	  && d_left (dc)->u.s_operator.op->name[0] == '>')
	d_append_char (dpi, ')');

      return;

    case DEMANGLE_COMPONENT_BINARY_ARGS:
      /* We should only see this as part of DEMANGLE_COMPONENT_BINARY.  */
      d_print_error (dpi);
      return;

    case DEMANGLE_COMPONENT_TRINARY:
      if (d_right (dc)->type != DEMANGLE_COMPONENT_TRINARY_ARG1
	  || d_right (d_right (dc))->type != DEMANGLE_COMPONENT_TRINARY_ARG2)
	{
	  d_print_error (dpi);
	  return;
	}
      d_append_char (dpi, '(');
      d_print_comp (dpi, d_left (d_right (dc)));
      d_append_string_constant (dpi, ") ");
      d_print_expr_op (dpi, d_left (dc));
      d_append_string_constant (dpi, " (");
      d_print_comp (dpi, d_left (d_right (d_right (dc))));
      d_append_string_constant (dpi, ") : (");
      d_print_comp (dpi, d_right (d_right (d_right (dc))));
      d_append_char (dpi, ')');
      return;

    case DEMANGLE_COMPONENT_TRINARY_ARG1:
    case DEMANGLE_COMPONENT_TRINARY_ARG2:
      /* We should only see these are part of DEMANGLE_COMPONENT_TRINARY.  */
      d_print_error (dpi);
      return;

    case DEMANGLE_COMPONENT_LITERAL:
    case DEMANGLE_COMPONENT_LITERAL_NEG:
      {
	enum d_builtin_type_print tp;

	/* For some builtin types, produce simpler output.  */
	tp = D_PRINT_DEFAULT;
	if (d_left (dc)->type == DEMANGLE_COMPONENT_BUILTIN_TYPE)
	  {
	    tp = d_left (dc)->u.s_builtin.type->print;
	    switch (tp)
	      {
	      case D_PRINT_INT:
	      case D_PRINT_UNSIGNED:
	      case D_PRINT_LONG:
	      case D_PRINT_UNSIGNED_LONG:
	      case D_PRINT_LONG_LONG:
	      case D_PRINT_UNSIGNED_LONG_LONG:
		if (d_right (dc)->type == DEMANGLE_COMPONENT_NAME)
		  {
		    if (dc->type == DEMANGLE_COMPONENT_LITERAL_NEG)
		      d_append_char (dpi, '-');
		    d_print_comp (dpi, d_right (dc));
		    switch (tp)
		      {
		      default:
			break;
		      case D_PRINT_UNSIGNED:
			d_append_char (dpi, 'u');
			break;
		      case D_PRINT_LONG:
			d_append_char (dpi, 'l');
			break;
		      case D_PRINT_UNSIGNED_LONG:
			d_append_string_constant (dpi, "ul");
			break;
		      case D_PRINT_LONG_LONG:
			d_append_string_constant (dpi, "ll");
			break;
		      case D_PRINT_UNSIGNED_LONG_LONG:
			d_append_string_constant (dpi, "ull");
			break;
		      }
		    return;
		  }
		break;

	      case D_PRINT_BOOL:
		if (d_right (dc)->type == DEMANGLE_COMPONENT_NAME
		    && d_right (dc)->u.s_name.len == 1
		    && dc->type == DEMANGLE_COMPONENT_LITERAL)
		  {
		    switch (d_right (dc)->u.s_name.s[0])
		      {
		      case '0':
			d_append_string_constant (dpi, "false");
			return;
		      case '1':
			d_append_string_constant (dpi, "true");
			return;
		      default:
			break;
		      }
		  }
		break;

	      default:
		break;
	      }
	  }

	d_append_char (dpi, '(');
	d_print_comp (dpi, d_left (dc));
	d_append_char (dpi, ')');
	if (dc->type == DEMANGLE_COMPONENT_LITERAL_NEG)
	  d_append_char (dpi, '-');
	if (tp == D_PRINT_FLOAT)
	  d_append_char (dpi, '[');
	d_print_comp (dpi, d_right (dc));
	if (tp == D_PRINT_FLOAT)
	  d_append_char (dpi, ']');
      }
      return;

    default:
      d_print_error (dpi);
      return;
    }
}

/* Print a Java dentifier.  For Java we try to handle encoded extended
   Unicode characters.  The C++ ABI doesn't mention Unicode encoding,
   so we don't it for C++.  Characters are encoded as
   __U<hex-char>+_.  */

static void
d_print_java_identifier (dpi, name, len)
     struct d_print_info *dpi;
     const char *name;
     int len;
{
  const char *p;
  const char *end;

  end = name + len;
  for (p = name; p < end; ++p)
    {
      if (end - p > 3
	  && p[0] == '_'
	  && p[1] == '_'
	  && p[2] == 'U')
	{
	  unsigned long c;
	  const char *q;

	  c = 0;
	  for (q = p + 3; q < end; ++q)
	    {
	      int dig;

	      if (IS_DIGIT (*q))
		dig = *q - '0';
	      else if (*q >= 'A' && *q <= 'F')
		dig = *q - 'A' + 10;
	      else if (*q >= 'a' && *q <= 'f')
		dig = *q - 'a' + 10;
	      else
		break;

	      c = c * 16 + dig;
	    }
	  /* If the Unicode character is larger than 256, we don't try
	     to deal with it here.  FIXME.  */
	  if (q < end && *q == '_' && c < 256)
	    {
	      d_append_char (dpi, c);
	      p = q;
	      continue;
	    }
	}

      d_append_char (dpi, *p);
    }
}

/* Print a list of modifiers.  SUFFIX is 1 if we are printing
   qualifiers on this after printing a function.  */

static void
d_print_mod_list (dpi, mods, suffix)
     struct d_print_info *dpi;
     struct d_print_mod *mods;
     int suffix;
{
  struct d_print_template *hold_dpt;

  if (mods == NULL || d_print_saw_error (dpi))
    return;

  if (mods->printed
      || (! suffix
	  && (mods->mod->type == DEMANGLE_COMPONENT_RESTRICT_THIS
	      || mods->mod->type == DEMANGLE_COMPONENT_VOLATILE_THIS
	      || mods->mod->type == DEMANGLE_COMPONENT_CONST_THIS)))
    {
      d_print_mod_list (dpi, mods->next, suffix);
      return;
    }

  mods->printed = 1;

  hold_dpt = dpi->templates;
  dpi->templates = mods->templates;

  if (mods->mod->type == DEMANGLE_COMPONENT_FUNCTION_TYPE)
    {
      d_print_function_type (dpi, mods->mod, mods->next);
      dpi->templates = hold_dpt;
      return;
    }
  else if (mods->mod->type == DEMANGLE_COMPONENT_ARRAY_TYPE)
    {
      d_print_array_type (dpi, mods->mod, mods->next);
      dpi->templates = hold_dpt;
      return;
    }
  else if (mods->mod->type == DEMANGLE_COMPONENT_LOCAL_NAME)
    {
      struct d_print_mod *hold_modifiers;
      struct demangle_component *dc;

      /* When this is on the modifier stack, we have pulled any
	 qualifiers off the right argument already.  Otherwise, we
	 print it as usual, but don't let the left argument see any
	 modifiers.  */

      hold_modifiers = dpi->modifiers;
      dpi->modifiers = NULL;
      d_print_comp (dpi, d_left (mods->mod));
      dpi->modifiers = hold_modifiers;

      if ((dpi->options & DMGL_JAVA) == 0)
	d_append_string_constant (dpi, "::");
      else
	d_append_char (dpi, '.');

      dc = d_right (mods->mod);
      while (dc->type == DEMANGLE_COMPONENT_RESTRICT_THIS
	     || dc->type == DEMANGLE_COMPONENT_VOLATILE_THIS
	     || dc->type == DEMANGLE_COMPONENT_CONST_THIS)
	dc = d_left (dc);

      d_print_comp (dpi, dc);

      dpi->templates = hold_dpt;
      return;
    }

  d_print_mod (dpi, mods->mod);

  dpi->templates = hold_dpt;

  d_print_mod_list (dpi, mods->next, suffix);
}

/* Print a modifier.  */

static void
d_print_mod (dpi, mod)
     struct d_print_info *dpi;
     const struct demangle_component *mod;
{
  switch (mod->type)
    {
    case DEMANGLE_COMPONENT_RESTRICT:
    case DEMANGLE_COMPONENT_RESTRICT_THIS:
      d_append_string_constant (dpi, " restrict");
      return;
    case DEMANGLE_COMPONENT_VOLATILE:
    case DEMANGLE_COMPONENT_VOLATILE_THIS:
      d_append_string_constant (dpi, " volatile");
      return;
    case DEMANGLE_COMPONENT_CONST:
    case DEMANGLE_COMPONENT_CONST_THIS:
      d_append_string_constant (dpi, " const");
      return;
    case DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL:
      d_append_char (dpi, ' ');
      d_print_comp (dpi, d_right (mod));
      return;
    case DEMANGLE_COMPONENT_POINTER:
      /* There is no pointer symbol in Java.  */
      if ((dpi->options & DMGL_JAVA) == 0)
	d_append_char (dpi, '*');
      return;
    case DEMANGLE_COMPONENT_REFERENCE:
      d_append_char (dpi, '&');
      return;
    case DEMANGLE_COMPONENT_COMPLEX:
      d_append_string_constant (dpi, "complex ");
      return;
    case DEMANGLE_COMPONENT_IMAGINARY:
      d_append_string_constant (dpi, "imaginary ");
      return;
    case DEMANGLE_COMPONENT_PTRMEM_TYPE:
      if (d_last_char (dpi) != '(')
	d_append_char (dpi, ' ');
      d_print_comp (dpi, d_left (mod));
      d_append_string_constant (dpi, "::*");
      return;
    case DEMANGLE_COMPONENT_TYPED_NAME:
      d_print_comp (dpi, d_left (mod));
      return;
    default:
      /* Otherwise, we have something that won't go back on the
	 modifier stack, so we can just print it.  */
      d_print_comp (dpi, mod);
      return;
    }
}

/* Print a function type, except for the return type.  */

static void
d_print_function_type (dpi, dc, mods)
     struct d_print_info *dpi;
     const struct demangle_component *dc;
     struct d_print_mod *mods;
{
  int need_paren;
  int saw_mod;
  int need_space;
  struct d_print_mod *p;
  struct d_print_mod *hold_modifiers;

  need_paren = 0;
  saw_mod = 0;
  need_space = 0;
  for (p = mods; p != NULL; p = p->next)
    {
      if (p->printed)
	break;

      saw_mod = 1;
      switch (p->mod->type)
	{
	case DEMANGLE_COMPONENT_POINTER:
	case DEMANGLE_COMPONENT_REFERENCE:
	  need_paren = 1;
	  break;
	case DEMANGLE_COMPONENT_RESTRICT:
	case DEMANGLE_COMPONENT_VOLATILE:
	case DEMANGLE_COMPONENT_CONST:
	case DEMANGLE_COMPONENT_VENDOR_TYPE_QUAL:
	case DEMANGLE_COMPONENT_COMPLEX:
	case DEMANGLE_COMPONENT_IMAGINARY:
	case DEMANGLE_COMPONENT_PTRMEM_TYPE:
	  need_space = 1;
	  need_paren = 1;
	  break;
	case DEMANGLE_COMPONENT_RESTRICT_THIS:
	case DEMANGLE_COMPONENT_VOLATILE_THIS:
	case DEMANGLE_COMPONENT_CONST_THIS:
	  break;
	default:
	  break;
	}
      if (need_paren)
	break;
    }

  if (d_left (dc) != NULL && ! saw_mod)
    need_paren = 1;

  if (need_paren)
    {
      if (! need_space)
	{
	  if (d_last_char (dpi) != '('
	      && d_last_char (dpi) != '*')
	    need_space = 1;
	}
      if (need_space && d_last_char (dpi) != ' ')
	d_append_char (dpi, ' ');
      d_append_char (dpi, '(');
    }

  hold_modifiers = dpi->modifiers;
  dpi->modifiers = NULL;

  d_print_mod_list (dpi, mods, 0);

  if (need_paren)
    d_append_char (dpi, ')');

  d_append_char (dpi, '(');

  if (d_right (dc) != NULL)
    d_print_comp (dpi, d_right (dc));

  d_append_char (dpi, ')');

  d_print_mod_list (dpi, mods, 1);

  dpi->modifiers = hold_modifiers;
}

/* Print an array type, except for the element type.  */

static void
d_print_array_type (dpi, dc, mods)
     struct d_print_info *dpi;
     const struct demangle_component *dc;
     struct d_print_mod *mods;
{
  int need_space;

  need_space = 1;
  if (mods != NULL)
    {
      int need_paren;
      struct d_print_mod *p;

      need_paren = 0;
      for (p = mods; p != NULL; p = p->next)
	{
	  if (! p->printed)
	    {
	      if (p->mod->type == DEMANGLE_COMPONENT_ARRAY_TYPE)
		{
		  need_space = 0;
		  break;
		}
	      else
		{
		  need_paren = 1;
		  need_space = 1;
		  break;
		}
	    }
	}

      if (need_paren)
	d_append_string_constant (dpi, " (");

      d_print_mod_list (dpi, mods, 0);

      if (need_paren)
	d_append_char (dpi, ')');
    }

  if (need_space)
    d_append_char (dpi, ' ');

  d_append_char (dpi, '[');

  if (d_left (dc) != NULL)
    d_print_comp (dpi, d_left (dc));

  d_append_char (dpi, ']');
}

/* Print an operator in an expression.  */

static void
d_print_expr_op (dpi, dc)
     struct d_print_info *dpi;
     const struct demangle_component *dc;
{
  if (dc->type == DEMANGLE_COMPONENT_OPERATOR)
    d_append_buffer (dpi, dc->u.s_operator.op->name,
		     dc->u.s_operator.op->len);
  else
    d_print_comp (dpi, dc);
}

/* Print a cast.  */

static void
d_print_cast (dpi, dc)
     struct d_print_info *dpi;
     const struct demangle_component *dc;
{
  if (d_left (dc)->type != DEMANGLE_COMPONENT_TEMPLATE)
    d_print_comp (dpi, d_left (dc));
  else
    {
      struct d_print_mod *hold_dpm;
      struct d_print_template dpt;

      /* It appears that for a templated cast operator, we need to put
	 the template parameters in scope for the operator name, but
	 not for the parameters.  The effect is that we need to handle
	 the template printing here.  */

      hold_dpm = dpi->modifiers;
      dpi->modifiers = NULL;

      dpt.next = dpi->templates;
      dpi->templates = &dpt;
      dpt.template = d_left (dc);

      d_print_comp (dpi, d_left (d_left (dc)));

      dpi->templates = dpt.next;

      if (d_last_char (dpi) == '<')
	d_append_char (dpi, ' ');
      d_append_char (dpi, '<');
      d_print_comp (dpi, d_right (d_left (dc)));
      /* Avoid generating two consecutive '>' characters, to avoid
	 the C++ syntactic ambiguity.  */
      if (d_last_char (dpi) == '>')
	d_append_char (dpi, ' ');
      d_append_char (dpi, '>');

      dpi->modifiers = hold_dpm;
    }
}

/* Initialize the information structure we use to pass around
   information.  */

CP_STATIC_IF_GLIBCPP_V3
void
cplus_demangle_init_info (mangled, options, len, di)
     const char *mangled;
     int options;
     size_t len;
     struct d_info *di;
{
  di->s = mangled;
  di->send = mangled + len;
  di->options = options;

  di->n = mangled;

  /* We can not need more components than twice the number of chars in
     the mangled string.  Most components correspond directly to
     chars, but the ARGLIST types are exceptions.  */
  di->num_comps = 2 * len;
  di->next_comp = 0;

  /* Similarly, we can not need more substitutions than there are
     chars in the mangled string.  */
  di->num_subs = len;
  di->next_sub = 0;
  di->did_subs = 0;

  di->last_name = NULL;

  di->expansion = 0;
}

/* Entry point for the demangler.  If MANGLED is a g++ v3 ABI mangled
   name, return a buffer allocated with malloc holding the demangled
   name.  OPTIONS is the usual libiberty demangler options.  On
   success, this sets *PALC to the allocated size of the returned
   buffer.  On failure, this sets *PALC to 0 for a bad name, or 1 for
   a memory allocation failure.  On failure, this returns NULL.  */

static char *
d_demangle (mangled, options, palc)
     const char* mangled;
     int options;
     size_t *palc;
{
  size_t len;
  int type;
  struct d_info di;
  struct demangle_component *dc;
  int estimate;
  char *ret;

  *palc = 0;

  len = strlen (mangled);

  if (mangled[0] == '_' && mangled[1] == 'Z')
    type = 0;
  else if (strncmp (mangled, "_GLOBAL_", 8) == 0
	   && (mangled[8] == '.' || mangled[8] == '_' || mangled[8] == '$')
	   && (mangled[9] == 'D' || mangled[9] == 'I')
	   && mangled[10] == '_')
    {
      char *r;

      r = malloc (40 + len - 11);
      if (r == NULL)
	*palc = 1;
      else
	{
	  if (mangled[9] == 'I')
	    strcpy (r, "global constructors keyed to ");
	  else
	    strcpy (r, "global destructors keyed to ");
	  strcat (r, mangled + 11);
	}
      return r;
    }
  else
    {
      if ((options & DMGL_TYPES) == 0)
	return NULL;
      type = 1;
    }

  cplus_demangle_init_info (mangled, options, len, &di);

  {
#ifdef CP_DYNAMIC_ARRAYS
    __extension__ struct demangle_component comps[di.num_comps];
    __extension__ struct demangle_component *subs[di.num_subs];

    di.comps = &comps[0];
    di.subs = &subs[0];
#else
    di.comps = ((struct demangle_component *)
		malloc (di.num_comps * sizeof (struct demangle_component)));
    di.subs = ((struct demangle_component **)
	       malloc (di.num_subs * sizeof (struct demangle_component *)));
    if (di.comps == NULL || di.subs == NULL)
      {
	if (di.comps != NULL)
	  free (di.comps);
	if (di.subs != NULL)
	  free (di.subs);
	*palc = 1;
	return NULL;
      }
#endif

    if (! type)
      dc = cplus_demangle_mangled_name (&di, 1);
    else
      dc = cplus_demangle_type (&di);

    /* If DMGL_PARAMS is set, then if we didn't consume the entire
       mangled string, then we didn't successfully demangle it.  If
       DMGL_PARAMS is not set, we didn't look at the trailing
       parameters.  */
    if (((options & DMGL_PARAMS) != 0) && d_peek_char (&di) != '\0')
      dc = NULL;

#ifdef CP_DEMANGLE_DEBUG
    if (dc == NULL)
      printf ("failed demangling\n");
    else
      d_dump (dc, 0);
#endif

    /* We try to guess the length of the demangled string, to minimize
       calls to realloc during demangling.  */
    estimate = len + di.expansion + 10 * di.did_subs;
    estimate += estimate / 8;

    ret = NULL;
    if (dc != NULL)
      ret = cplus_demangle_print (options, dc, estimate, palc);

#ifndef CP_DYNAMIC_ARRAYS
    free (di.comps);
    free (di.subs);
#endif

#ifdef CP_DEMANGLE_DEBUG
    if (ret != NULL)
      {
	int rlen;

	rlen = strlen (ret);
	if (rlen > 2 * estimate)
	  printf ("*** Length %d much greater than estimate %d\n",
		  rlen, estimate);
	else if (rlen > estimate)
	  printf ("*** Length %d greater than estimate %d\n",
		  rlen, estimate);
	else if (rlen < estimate / 2)
	  printf ("*** Length %d much less than estimate %d\n",
		  rlen, estimate);
      }
#endif
  }

  return ret;
}

static char *
cplus_demangle_v3 (mangled, options)
     const char* mangled;
     int options;
{
  size_t alc;

  return d_demangle (mangled, options, &alc);
}

/* Demangle a Java symbol.  Java uses a subset of the V3 ABI C++ mangling 
   conventions, but the output formatting is a little different.
   This instructs the C++ demangler not to emit pointer characters ("*"), and 
   to use Java's namespace separator symbol ("." instead of "::").  It then 
   does an additional pass over the demangled output to replace instances 
   of JArray<TYPE> with TYPE[].  */

static char *
java_demangle_v3 (mangled)
     const char* mangled;
{
  size_t alc;
  char *demangled;
  int nesting;
  char *from;
  char *to;

  demangled = d_demangle (mangled, DMGL_JAVA | DMGL_PARAMS, &alc);

  if (demangled == NULL)
    return NULL;

  nesting = 0;
  from = demangled;
  to = from;
  while (*from != '\0')
    {
      if (strncmp (from, "JArray<", 7) == 0)
	{
	  from += 7;
	  ++nesting;
	}
      else if (nesting > 0 && *from == '>')
	{
	  while (to > demangled && to[-1] == ' ')
	    --to;
	  *to++ = '[';
	  *to++ = ']';
	  --nesting;
	  ++from;
	}
      else
	*to++ = *from++;
    }

  *to = '\0';

  return demangled;
}

#ifndef IN_GLIBCPP_V3

/* Demangle a string in order to find out whether it is a constructor
   or destructor.  Return non-zero on success.  Set *CTOR_KIND and
   *DTOR_KIND appropriately.  */

static int
is_ctor_or_dtor (mangled, ctor_kind, dtor_kind)
     const char *mangled;
     enum gnu_v3_ctor_kinds *ctor_kind;
     enum gnu_v3_dtor_kinds *dtor_kind;
{
  struct d_info di;
  struct demangle_component *dc;
  int ret;

  *ctor_kind = (enum gnu_v3_ctor_kinds) 0;
  *dtor_kind = (enum gnu_v3_dtor_kinds) 0;

  cplus_demangle_init_info (mangled, DMGL_GNU_V3, strlen (mangled), &di);

  {
#ifdef CP_DYNAMIC_ARRAYS
    __extension__ struct demangle_component comps[di.num_comps];
    __extension__ struct demangle_component *subs[di.num_subs];

    di.comps = &comps[0];
    di.subs = &subs[0];
#else
    di.comps = ((struct demangle_component *)
		malloc (di.num_comps * sizeof (struct demangle_component)));
    di.subs = ((struct demangle_component **)
	       malloc (di.num_subs * sizeof (struct demangle_component *)));
    if (di.comps == NULL || di.subs == NULL)
      {
	if (di.comps != NULL)
	  free (di.comps);
	if (di.subs != NULL)
	  free (di.subs);
	return 0;
      }
#endif

    dc = cplus_demangle_mangled_name (&di, 1);

    /* Note that because we did not pass DMGL_PARAMS, we don't expect
       to demangle the entire string.  */

    ret = 0;
    while (dc != NULL)
      {
	switch (dc->type)
	  {
	  default:
	    dc = NULL;
	    break;
	  case DEMANGLE_COMPONENT_TYPED_NAME:
	  case DEMANGLE_COMPONENT_TEMPLATE:
	  case DEMANGLE_COMPONENT_RESTRICT_THIS:
	  case DEMANGLE_COMPONENT_VOLATILE_THIS:
	  case DEMANGLE_COMPONENT_CONST_THIS:
	    dc = d_left (dc);
	    break;
	  case DEMANGLE_COMPONENT_QUAL_NAME:
	  case DEMANGLE_COMPONENT_LOCAL_NAME:
	    dc = d_right (dc);
	    break;
	  case DEMANGLE_COMPONENT_CTOR:
	    *ctor_kind = dc->u.s_ctor.kind;
	    ret = 1;
	    dc = NULL;
	    break;
	  case DEMANGLE_COMPONENT_DTOR:
	    *dtor_kind = dc->u.s_dtor.kind;
	    ret = 1;
	    dc = NULL;
	    break;
	  }
      }

#ifndef CP_DYNAMIC_ARRAYS
    free (di.subs);
    free (di.comps);
#endif
  }

  return ret;
}

/* Return whether NAME is the mangled form of a g++ V3 ABI constructor
   name.  A non-zero return indicates the type of constructor.  */

enum gnu_v3_ctor_kinds
is_gnu_v3_mangled_ctor (name)
     const char *name;
{
  enum gnu_v3_ctor_kinds ctor_kind;
  enum gnu_v3_dtor_kinds dtor_kind;

  if (! is_ctor_or_dtor (name, &ctor_kind, &dtor_kind))
    return (enum gnu_v3_ctor_kinds) 0;
  return ctor_kind;
}


/* Return whether NAME is the mangled form of a g++ V3 ABI destructor
   name.  A non-zero return indicates the type of destructor.  */

enum gnu_v3_dtor_kinds
is_gnu_v3_mangled_dtor (name)
     const char *name;
{
  enum gnu_v3_ctor_kinds ctor_kind;
  enum gnu_v3_dtor_kinds dtor_kind;

  if (! is_ctor_or_dtor (name, &ctor_kind, &dtor_kind))
    return (enum gnu_v3_dtor_kinds) 0;
  return dtor_kind;
}

#endif /* IN_GLIBCPP_V3 */
/* Demangler for GNU C++
   Copyright 1989, 1991, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.
   Written by James Clark (jjc@jclark.uucp)
   Rewritten by Fred Fish (fnf@cygnus.com) for ARM and Lucid demangling
   Modified by Satish Pai (pai@apollo.hp.com) for HP demangling

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

In addition to the permissions in the GNU Library General Public
License, the Free Software Foundation gives you unlimited permission
to link the compiled version of this file into combinations with other
programs, and to distribute those combinations without any restriction
coming from the use of this file.  (The Library Public License
restrictions do apply in other respects; for example, they cover
modification of the file, and distribution when not linked into a
combined executable.)

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* This file exports two functions; cplus_mangle_opname and cplus_demangle.

   This file imports xmalloc and g_realloc, which are like malloc and
   realloc except that they generate a fatal error if there is no
   available memory.  */

/* This file lives in both GCC and libiberty.  When making changes, please
   try not to break either.  */

#undef CURRENT_DEMANGLING_STYLE
#define CURRENT_DEMANGLING_STYLE work->options

static char *ada_demangle  PARAMS ((const char *, int));

#define min(X,Y) (((X) < (Y)) ? (X) : (Y))

/* A value at least one greater than the maximum number of characters
   that will be output when using the `%d' format with `printf'.  */
#define INTBUF_SIZE 32

extern void fancy_abort PARAMS ((void)) ATTRIBUTE_NORETURN;

/* In order to allow a single demangler executable to demangle strings
   using various common values of CPLUS_MARKER, as well as any specific
   one set at compile time, we maintain a string containing all the
   commonly used ones, and check to see if the marker we are looking for
   is in that string.  CPLUS_MARKER is usually '$' on systems where the
   assembler can deal with that.  Where the assembler can't, it's usually
   '.' (but on many systems '.' is used for other things).  We put the
   current defined CPLUS_MARKER first (which defaults to '$'), followed
   by the next most common value, followed by an explicit '$' in case
   the value of CPLUS_MARKER is not '$'.

   We could avoid this if we could just get g++ to tell us what the actual
   cplus marker character is as part of the debug information, perhaps by
   ensuring that it is the character that terminates the gcc<n>_compiled
   marker symbol (FIXME).  */

#if !defined (CPLUS_MARKER)
#define CPLUS_MARKER '$'
#endif

static enum demangling_styles current_demangling_style = auto_demangling;

static char cplus_markers[] = { CPLUS_MARKER, '.', '$', '\0' };

static char char_str[2] = { '\000', '\000' };

typedef struct string		/* Beware: these aren't required to be */
{				/*  '\0' terminated.  */
  char *b;			/* pointer to start of string */
  char *p;			/* pointer after last character */
  char *e;			/* pointer after end of allocated space */
} string;

/* Stuff that is shared between sub-routines.
   Using a shared structure allows cplus_demangle to be reentrant.  */

struct work_stuff
{
  int options;
  char **typevec;
  char **ktypevec;
  char **btypevec;
  int numk;
  int numb;
  int ksize;
  int bsize;
  int ntypes;
  int typevec_size;
  int constructor;
  int destructor;
  int static_type;	/* A static member function */
  int temp_start;       /* index in demangled to start of template args */
  int type_quals;       /* The type qualifiers.  */
  int dllimported;	/* Symbol imported from a PE DLL */
  char **tmpl_argvec;   /* Template function arguments. */
  int ntmpl_args;       /* The number of template function arguments. */
  int forgetting_types; /* Nonzero if we are not remembering the types
			   we see.  */
  string* previous_argument; /* The last function argument demangled.  */
  int nrepeats;         /* The number of times to repeat the previous
			   argument.  */
};

#define PRINT_ANSI_QUALIFIERS (work -> options & DMGL_ANSI)
#define PRINT_ARG_TYPES       (work -> options & DMGL_PARAMS)

static const struct optable
{
  const char *const in;
  const char *const out;
  const int flags;
} optable[] = {
  {"nw",	  " new",	DMGL_ANSI},	/* new (1.92,	 ansi) */
  {"dl",	  " delete",	DMGL_ANSI},	/* new (1.92,	 ansi) */
  {"new",	  " new",	0},		/* old (1.91,	 and 1.x) */
  {"delete",	  " delete",	0},		/* old (1.91,	 and 1.x) */
  {"vn",	  " new []",	DMGL_ANSI},	/* GNU, pending ansi */
  {"vd",	  " delete []",	DMGL_ANSI},	/* GNU, pending ansi */
  {"as",	  "=",		DMGL_ANSI},	/* ansi */
  {"ne",	  "!=",		DMGL_ANSI},	/* old, ansi */
  {"eq",	  "==",		DMGL_ANSI},	/* old,	ansi */
  {"ge",	  ">=",		DMGL_ANSI},	/* old,	ansi */
  {"gt",	  ">",		DMGL_ANSI},	/* old,	ansi */
  {"le",	  "<=",		DMGL_ANSI},	/* old,	ansi */
  {"lt",	  "<",		DMGL_ANSI},	/* old,	ansi */
  {"plus",	  "+",		0},		/* old */
  {"pl",	  "+",		DMGL_ANSI},	/* ansi */
  {"apl",	  "+=",		DMGL_ANSI},	/* ansi */
  {"minus",	  "-",		0},		/* old */
  {"mi",	  "-",		DMGL_ANSI},	/* ansi */
  {"ami",	  "-=",		DMGL_ANSI},	/* ansi */
  {"mult",	  "*",		0},		/* old */
  {"ml",	  "*",		DMGL_ANSI},	/* ansi */
  {"amu",	  "*=",		DMGL_ANSI},	/* ansi (ARM/Lucid) */
  {"aml",	  "*=",		DMGL_ANSI},	/* ansi (GNU/g++) */
  {"convert",	  "+",		0},		/* old (unary +) */
  {"negate",	  "-",		0},		/* old (unary -) */
  {"trunc_mod",	  "%",		0},		/* old */
  {"md",	  "%",		DMGL_ANSI},	/* ansi */
  {"amd",	  "%=",		DMGL_ANSI},	/* ansi */
  {"trunc_div",	  "/",		0},		/* old */
  {"dv",	  "/",		DMGL_ANSI},	/* ansi */
  {"adv",	  "/=",		DMGL_ANSI},	/* ansi */
  {"truth_andif", "&&",		0},		/* old */
  {"aa",	  "&&",		DMGL_ANSI},	/* ansi */
  {"truth_orif",  "||",		0},		/* old */
  {"oo",	  "||",		DMGL_ANSI},	/* ansi */
  {"truth_not",	  "!",		0},		/* old */
  {"nt",	  "!",		DMGL_ANSI},	/* ansi */
  {"postincrement","++",	0},		/* old */
  {"pp",	  "++",		DMGL_ANSI},	/* ansi */
  {"postdecrement","--",	0},		/* old */
  {"mm",	  "--",		DMGL_ANSI},	/* ansi */
  {"bit_ior",	  "|",		0},		/* old */
  {"or",	  "|",		DMGL_ANSI},	/* ansi */
  {"aor",	  "|=",		DMGL_ANSI},	/* ansi */
  {"bit_xor",	  "^",		0},		/* old */
  {"er",	  "^",		DMGL_ANSI},	/* ansi */
  {"aer",	  "^=",		DMGL_ANSI},	/* ansi */
  {"bit_and",	  "&",		0},		/* old */
  {"ad",	  "&",		DMGL_ANSI},	/* ansi */
  {"aad",	  "&=",		DMGL_ANSI},	/* ansi */
  {"bit_not",	  "~",		0},		/* old */
  {"co",	  "~",		DMGL_ANSI},	/* ansi */
  {"call",	  "()",		0},		/* old */
  {"cl",	  "()",		DMGL_ANSI},	/* ansi */
  {"alshift",	  "<<",		0},		/* old */
  {"ls",	  "<<",		DMGL_ANSI},	/* ansi */
  {"als",	  "<<=",	DMGL_ANSI},	/* ansi */
  {"arshift",	  ">>",		0},		/* old */
  {"rs",	  ">>",		DMGL_ANSI},	/* ansi */
  {"ars",	  ">>=",	DMGL_ANSI},	/* ansi */
  {"component",	  "->",		0},		/* old */
  {"pt",	  "->",		DMGL_ANSI},	/* ansi; Lucid C++ form */
  {"rf",	  "->",		DMGL_ANSI},	/* ansi; ARM/GNU form */
  {"indirect",	  "*",		0},		/* old */
  {"method_call",  "->()",	0},		/* old */
  {"addr",	  "&",		0},		/* old (unary &) */
  {"array",	  "[]",		0},		/* old */
  {"vc",	  "[]",		DMGL_ANSI},	/* ansi */
  {"compound",	  ", ",		0},		/* old */
  {"cm",	  ", ",		DMGL_ANSI},	/* ansi */
  {"cond",	  "?:",		0},		/* old */
  {"cn",	  "?:",		DMGL_ANSI},	/* pseudo-ansi */
  {"max",	  ">?",		0},		/* old */
  {"mx",	  ">?",		DMGL_ANSI},	/* pseudo-ansi */
  {"min",	  "<?",		0},		/* old */
  {"mn",	  "<?",		DMGL_ANSI},	/* pseudo-ansi */
  {"nop",	  "",		0},		/* old (for operator=) */
  {"rm",	  "->*",	DMGL_ANSI},	/* ansi */
  {"sz",          "sizeof ",    DMGL_ANSI}      /* pseudo-ansi */
};

/* These values are used to indicate the various type varieties.
   They are all non-zero so that they can be used as `success'
   values.  */
typedef enum type_kind_t
{
  tk_none,
  tk_pointer,
  tk_reference,
  tk_integral,
  tk_bool,
  tk_char,
  tk_real
} type_kind_t;

static const struct demangler_engine libiberty_demanglers[] =
{
  {
    NO_DEMANGLING_STYLE_STRING,
    no_demangling,
    "Demangling disabled"
  }
  ,
  {
    AUTO_DEMANGLING_STYLE_STRING,
      auto_demangling,
      "Automatic selection based on executable"
  }
  ,
  {
    GNU_DEMANGLING_STYLE_STRING,
      gnu_demangling,
      "GNU (g++) style demangling"
  }
  ,
  {
    LUCID_DEMANGLING_STYLE_STRING,
      lucid_demangling,
      "Lucid (lcc) style demangling"
  }
  ,
  {
    ARM_DEMANGLING_STYLE_STRING,
      arm_demangling,
      "ARM style demangling"
  }
  ,
  {
    HP_DEMANGLING_STYLE_STRING,
      hp_demangling,
      "HP (aCC) style demangling"
  }
  ,
  {
    EDG_DEMANGLING_STYLE_STRING,
      edg_demangling,
      "EDG style demangling"
  }
  ,
  {
    GNU_V3_DEMANGLING_STYLE_STRING,
    gnu_v3_demangling,
    "GNU (g++) V3 ABI-style demangling"
  }
  ,
  {
    JAVA_DEMANGLING_STYLE_STRING,
    java_demangling,
    "Java style demangling"
  }
  ,
  {
    GNAT_DEMANGLING_STYLE_STRING,
    gnat_demangling,
    "GNAT style demangling"
  }
  ,
  {
    NULL, unknown_demangling, NULL
  }
};

#define STRING_EMPTY(str)	((str) -> b == (str) -> p)
#define APPEND_BLANK(str)	{if (!STRING_EMPTY(str)) \
    string_append(str, " ");}
#define LEN_STRING(str)         ( (STRING_EMPTY(str))?0:((str)->p - (str)->b))

/* The scope separator appropriate for the language being demangled.  */

#define SCOPE_STRING(work) ((work->options & DMGL_JAVA) ? "." : "::")

#define ARM_VTABLE_STRING "__vtbl__"	/* Lucid/ARM virtual table prefix */
#define ARM_VTABLE_STRLEN 8		/* strlen (ARM_VTABLE_STRING) */

/* Prototypes for local functions */

static void
delete_work_stuff PARAMS ((struct work_stuff *));

static void
delete_non_B_K_work_stuff PARAMS ((struct work_stuff *));

static char *
mop_up PARAMS ((struct work_stuff *, string *, int));

static void
squangle_mop_up PARAMS ((struct work_stuff *));

static void
work_stuff_copy_to_from PARAMS ((struct work_stuff *, struct work_stuff *));

#if 0
static int
demangle_method_args PARAMS ((struct work_stuff *, const char **, string *));
#endif

static char *
internal_cplus_demangle PARAMS ((struct work_stuff *, const char *));

static int
demangle_template_template_parm PARAMS ((struct work_stuff *work,
					 const char **, string *));

static int
demangle_template PARAMS ((struct work_stuff *work, const char **, string *,
			   string *, int, int));

static int
arm_pt PARAMS ((struct work_stuff *, const char *, int, const char **,
		const char **));

static int
demangle_class_name PARAMS ((struct work_stuff *, const char **, string *));

static int
demangle_qualified PARAMS ((struct work_stuff *, const char **, string *,
			    int, int));

static int
demangle_class PARAMS ((struct work_stuff *, const char **, string *));

static int
demangle_fund_type PARAMS ((struct work_stuff *, const char **, string *));

static int
demangle_signature PARAMS ((struct work_stuff *, const char **, string *));

static int
demangle_prefix PARAMS ((struct work_stuff *, const char **, string *));

static int
gnu_special PARAMS ((struct work_stuff *, const char **, string *));

static int
arm_special PARAMS ((const char **, string *));

static void
string_need PARAMS ((string *, int));

static void
string_delete PARAMS ((string *));

static void
string_init PARAMS ((string *));

static void
string_clear PARAMS ((string *));

#if 0
static int
string_empty PARAMS ((string *));
#endif

static void
string_append PARAMS ((string *, const char *));

static void
string_appends PARAMS ((string *, string *));

static void
string_appendn PARAMS ((string *, const char *, int));

static void
string_prepend PARAMS ((string *, const char *));

static void
string_prependn PARAMS ((string *, const char *, int));

static void
string_append_template_idx PARAMS ((string *, int));

static int
get_count PARAMS ((const char **, int *));

static int
consume_count PARAMS ((const char **));

static int
consume_count_with_underscores PARAMS ((const char**));

static int
demangle_args PARAMS ((struct work_stuff *, const char **, string *));

static int
demangle_nested_args PARAMS ((struct work_stuff*, const char**, string*));

static int
do_type PARAMS ((struct work_stuff *, const char **, string *));

static int
do_arg PARAMS ((struct work_stuff *, const char **, string *));

static void
demangle_function_name PARAMS ((struct work_stuff *, const char **, string *,
				const char *));

static int
iterate_demangle_function PARAMS ((struct work_stuff *,
				   const char **, string *, const char *));

static void
remember_type PARAMS ((struct work_stuff *, const char *, int));

static void
remember_Btype PARAMS ((struct work_stuff *, const char *, int, int));

static int
register_Btype PARAMS ((struct work_stuff *));

static void
remember_Ktype PARAMS ((struct work_stuff *, const char *, int));

static void
forget_types PARAMS ((struct work_stuff *));

static void
forget_B_and_K_types PARAMS ((struct work_stuff *));

static void
string_prepends PARAMS ((string *, string *));

static int
demangle_template_value_parm PARAMS ((struct work_stuff*, const char**,
				      string*, type_kind_t));

static int
do_hpacc_template_const_value PARAMS ((struct work_stuff *, const char **, string *));

static int
do_hpacc_template_literal PARAMS ((struct work_stuff *, const char **, string *));

static int
snarf_numeric_literal PARAMS ((const char **, string *));

/* There is a TYPE_QUAL value for each type qualifier.  They can be
   combined by bitwise-or to form the complete set of qualifiers for a
   type.  */

#define TYPE_UNQUALIFIED   0x0
#define TYPE_QUAL_CONST    0x1
#define TYPE_QUAL_VOLATILE 0x2
#define TYPE_QUAL_RESTRICT 0x4

static int
code_for_qualifier PARAMS ((int));

static const char*
qualifier_string PARAMS ((int));

static const char*
demangle_qualifier PARAMS ((int));

static int
demangle_expression PARAMS ((struct work_stuff *, const char **, string *, 
			     type_kind_t));

static int
demangle_integral_value PARAMS ((struct work_stuff *, const char **,
				 string *));

static int
demangle_real_value PARAMS ((struct work_stuff *, const char **, string *));

static void
demangle_arm_hp_template PARAMS ((struct work_stuff *, const char **, int,
				  string *));

static void
recursively_demangle PARAMS ((struct work_stuff *, const char **, string *,
			      int));

static void
grow_vect PARAMS ((char **, size_t *, size_t, int));

/* Translate count to integer, consuming tokens in the process.
   Conversion terminates on the first non-digit character.

   Trying to consume something that isn't a count results in no
   consumption of input and a return of -1.

   Overflow consumes the rest of the digits, and returns -1.  */

static int
consume_count (type)
     const char **type;
{
  int count = 0;

  if (! g_ascii_isdigit ((unsigned char)**type))
    return -1;

  while (g_ascii_isdigit ((unsigned char)**type))
    {
      count *= 10;

      /* Check for overflow.
	 We assume that count is represented using two's-complement;
	 no power of two is divisible by ten, so if an overflow occurs
	 when multiplying by ten, the result will not be a multiple of
	 ten.  */
      if ((count % 10) != 0)
	{
	  while (g_ascii_isdigit ((unsigned char) **type))
	    (*type)++;
	  return -1;
	}

      count += **type - '0';
      (*type)++;
    }

  if (count < 0)
    count = -1;

  return (count);
}


/* Like consume_count, but for counts that are preceded and followed
   by '_' if they are greater than 10.  Also, -1 is returned for
   failure, since 0 can be a valid value.  */

static int
consume_count_with_underscores (mangled)
     const char **mangled;
{
  int idx;

  if (**mangled == '_')
    {
      (*mangled)++;
      if (!g_ascii_isdigit ((unsigned char)**mangled))
	return -1;

      idx = consume_count (mangled);
      if (**mangled != '_')
	/* The trailing underscore was missing. */
	return -1;

      (*mangled)++;
    }
  else
    {
      if (**mangled < '0' || **mangled > '9')
	return -1;

      idx = **mangled - '0';
      (*mangled)++;
    }

  return idx;
}

/* C is the code for a type-qualifier.  Return the TYPE_QUAL
   corresponding to this qualifier.  */

static int
code_for_qualifier (c)
  int c;
{
  switch (c)
    {
    case 'C':
      return TYPE_QUAL_CONST;

    case 'V':
      return TYPE_QUAL_VOLATILE;

    case 'u':
      return TYPE_QUAL_RESTRICT;

    default:
      break;
    }

  /* C was an invalid qualifier.  */
  abort ();
}

/* Return the string corresponding to the qualifiers given by
   TYPE_QUALS.  */

static const char*
qualifier_string (type_quals)
     int type_quals;
{
  switch (type_quals)
    {
    case TYPE_UNQUALIFIED:
      return "";

    case TYPE_QUAL_CONST:
      return "const";

    case TYPE_QUAL_VOLATILE:
      return "volatile";

    case TYPE_QUAL_RESTRICT:
      return "__restrict";

    case TYPE_QUAL_CONST | TYPE_QUAL_VOLATILE:
      return "const volatile";

    case TYPE_QUAL_CONST | TYPE_QUAL_RESTRICT:
      return "const __restrict";

    case TYPE_QUAL_VOLATILE | TYPE_QUAL_RESTRICT:
      return "volatile __restrict";

    case TYPE_QUAL_CONST | TYPE_QUAL_VOLATILE | TYPE_QUAL_RESTRICT:
      return "const volatile __restrict";

    default:
      break;
    }

  /* TYPE_QUALS was an invalid qualifier set.  */
  abort ();
}

/* C is the code for a type-qualifier.  Return the string
   corresponding to this qualifier.  This function should only be
   called with a valid qualifier code.  */

static const char*
demangle_qualifier (c)
  int c;
{
  return qualifier_string (code_for_qualifier (c));
}

/* Takes operator name as e.g. "++" and returns mangled
   operator name (e.g. "postincrement_expr"), or NULL if not found.

   If OPTIONS & DMGL_ANSI == 1, return the ANSI name;
   if OPTIONS & DMGL_ANSI == 0, return the old GNU name.  */

/* Add a routine to set the demangling style to be sure it is valid and
   allow for any demangler initialization that maybe necessary. */

/* Do string name to style translation */

/* char *cplus_demangle (const char *mangled, int options)

   If MANGLED is a mangled function name produced by GNU C++, then
   a pointer to a @code{malloc}ed string giving a C++ representation
   of the name will be returned; otherwise NULL will be returned.
   It is the caller's responsibility to free the string which
   is returned.

   The OPTIONS arg may contain one or more of the following bits:

   	DMGL_ANSI	ANSI qualifiers such as `const' and `void' are
			included.
	DMGL_PARAMS	Function parameters are included.

   For example,

   cplus_demangle ("foo__1Ai", DMGL_PARAMS)		=> "A::foo(int)"
   cplus_demangle ("foo__1Ai", DMGL_PARAMS | DMGL_ANSI)	=> "A::foo(int)"
   cplus_demangle ("foo__1Ai", 0)			=> "A::foo"

   cplus_demangle ("foo__1Afe", DMGL_PARAMS)		=> "A::foo(float,...)"
   cplus_demangle ("foo__1Afe", DMGL_PARAMS | DMGL_ANSI)=> "A::foo(float,...)"
   cplus_demangle ("foo__1Afe", 0)			=> "A::foo"

   Note that any leading underscores, or other such characters prepended by
   the compilation system, are presumed to have already been stripped from
   MANGLED.  */

char *
sysprof_cplus_demangle (mangled, options)
     const char *mangled;
     int options;
{
  char *ret;
  struct work_stuff work[1];

  if (current_demangling_style == no_demangling)
    return g_strdup (mangled);

  memset ((char *) work, 0, sizeof (work));
  work->options = options;
  if ((work->options & DMGL_STYLE_MASK) == 0)
    work->options |= (int) current_demangling_style & DMGL_STYLE_MASK;

  /* The V3 ABI demangling is implemented elsewhere.  */
  if (GNU_V3_DEMANGLING || AUTO_DEMANGLING)
    {
      ret = cplus_demangle_v3 (mangled, work->options);
      if (ret || GNU_V3_DEMANGLING)
	return ret;
    }

  if (JAVA_DEMANGLING)
    {
      ret = java_demangle_v3 (mangled);
      if (ret)
        return ret;
    }

  if (GNAT_DEMANGLING)
    return ada_demangle(mangled,options);

  ret = internal_cplus_demangle (work, mangled);
  squangle_mop_up (work);
  return (ret);
}


/* Assuming *OLD_VECT points to an array of *SIZE objects of size
   ELEMENT_SIZE, grow it to contain at least MIN_SIZE objects,
   updating *OLD_VECT and *SIZE as necessary.  */

static void
grow_vect (old_vect, size, min_size, element_size)
     char **old_vect;
     size_t *size;
     size_t min_size;
     int element_size;
{
  if (*size < min_size)
    {
      *size *= 2;
      if (*size < min_size)
	*size = min_size;
      *old_vect = (void *) g_realloc (*old_vect, *size * element_size);
    }
}

/* Demangle ada names:
   1. Discard final __{DIGIT}+ or ${DIGIT}+
   2. Convert other instances of embedded "__" to `.'.
   3. Discard leading _ada_.
   4. Remove everything after first ___ if it is followed by 'X'.
   5. Put symbols that should be suppressed in <...> brackets.
   The resulting string is valid until the next call of ada_demangle.  */

static char *
ada_demangle (mangled, option)
     const char *mangled;
     int option ATTRIBUTE_UNUSED;
{
  int i, j;
  int len0;
  const char* p;
  char *demangled = NULL;
  int changed;
  size_t demangled_size = 0;
  
  changed = 0;

  if (strncmp (mangled, "_ada_", 5) == 0)
    {
      mangled += 5;
      changed = 1;
    }
  
  if (mangled[0] == '_' || mangled[0] == '<')
    goto Suppress;
  
  p = strstr (mangled, "___");
  if (p == NULL)
    len0 = strlen (mangled);
  else
    {
      if (p[3] == 'X')
	{
	  len0 = p - mangled;
	  changed = 1;
	}
      else
	goto Suppress;
    }
  
  /* Make demangled big enough for possible expansion by operator name.  */
  grow_vect (&demangled,
	     &demangled_size,  2 * len0 + 1,
	     sizeof (char));
  
  if (g_ascii_isdigit ((unsigned char) mangled[len0 - 1])) {
    for (i = len0 - 2; i >= 0 && g_ascii_isdigit ((unsigned char) mangled[i]); i -= 1)
      ;
    if (i > 1 && mangled[i] == '_' && mangled[i - 1] == '_')
      {
	len0 = i - 1;
	changed = 1;
      }
    else if (mangled[i] == '$')
      {
	len0 = i;
	changed = 1;
      }
  }
  
  for (i = 0, j = 0; i < len0 && ! g_ascii_isalpha ((unsigned char)mangled[i]);
       i += 1, j += 1)
    demangled[j] = mangled[i];
  
  while (i < len0)
    {
      if (i < len0 - 2 && mangled[i] == '_' && mangled[i + 1] == '_')
	{
	  demangled[j] = '.';
	  changed = 1;
	  i += 2; j += 1;
	}
      else
	{
	  demangled[j] = mangled[i];
	  i += 1;  j += 1;
	}
    }
  demangled[j] = '\000';
  
  for (i = 0; demangled[i] != '\0'; i += 1)
    if (g_ascii_isupper ((unsigned char)demangled[i]) || demangled[i] == ' ')
      goto Suppress;

  if (! changed)
    return NULL;
  else
    return demangled;
  
 Suppress:
  grow_vect (&demangled,
	     &demangled_size,  strlen (mangled) + 3,
	     sizeof (char));

  if (mangled[0] == '<')
     strcpy (demangled, mangled);
  else
    sprintf (demangled, "<%s>", mangled);

  return demangled;
}

/* This function performs most of what cplus_demangle use to do, but
   to be able to demangle a name with a B, K or n code, we need to
   have a longer term memory of what types have been seen. The original
   now initializes and cleans up the squangle code info, while internal
   calls go directly to this routine to avoid resetting that info. */

static char *
internal_cplus_demangle (work, mangled)
     struct work_stuff *work;
     const char *mangled;
{

  string decl;
  int success = 0;
  char *demangled = NULL;
  int s1, s2, s3, s4;
  s1 = work->constructor;
  s2 = work->destructor;
  s3 = work->static_type;
  s4 = work->type_quals;
  work->constructor = work->destructor = 0;
  work->type_quals = TYPE_UNQUALIFIED;
  work->dllimported = 0;

  if ((mangled != NULL) && (*mangled != '\0'))
    {
      string_init (&decl);

      /* First check to see if gnu style demangling is active and if the
	 string to be demangled contains a CPLUS_MARKER.  If so, attempt to
	 recognize one of the gnu special forms rather than looking for a
	 standard prefix.  In particular, don't worry about whether there
	 is a "__" string in the mangled string.  Consider "_$_5__foo" for
	 example.  */

      if ((AUTO_DEMANGLING || GNU_DEMANGLING))
	{
	  success = gnu_special (work, &mangled, &decl);
	}
      if (!success)
	{
	  success = demangle_prefix (work, &mangled, &decl);
	}
      if (success && (*mangled != '\0'))
	{
	  success = demangle_signature (work, &mangled, &decl);
	}
      if (work->constructor == 2)
        {
          string_prepend (&decl, "global constructors keyed to ");
          work->constructor = 0;
        }
      else if (work->destructor == 2)
        {
          string_prepend (&decl, "global destructors keyed to ");
          work->destructor = 0;
        }
      else if (work->dllimported == 1)
        {
          string_prepend (&decl, "import stub for ");
          work->dllimported = 0;
        }
      demangled = mop_up (work, &decl, success);
    }
  work->constructor = s1;
  work->destructor = s2;
  work->static_type = s3;
  work->type_quals = s4;
  return demangled;
}


/* Clear out and squangling related storage */
static void
squangle_mop_up (work)
     struct work_stuff *work;
{
  /* clean up the B and K type mangling types. */
  forget_B_and_K_types (work);
  if (work -> btypevec != NULL)
    {
      g_free ((char *) work -> btypevec);
    }
  if (work -> ktypevec != NULL)
    {
      g_free ((char *) work -> ktypevec);
    }
}


/* Copy the work state and storage.  */

static void
work_stuff_copy_to_from (to, from)
     struct work_stuff *to;
     struct work_stuff *from;
{
  int i;

  delete_work_stuff (to);

  /* Shallow-copy scalars.  */
  memcpy (to, from, sizeof (*to));

  /* Deep-copy dynamic storage.  */
  if (from->typevec_size)
    to->typevec
      = (char **) g_malloc (from->typevec_size * sizeof (to->typevec[0]));

  for (i = 0; i < from->ntypes; i++)
    {
      int len = strlen (from->typevec[i]) + 1;

      to->typevec[i] = g_malloc (len);
      memcpy (to->typevec[i], from->typevec[i], len);
    }

  if (from->ksize)
    to->ktypevec
      = (char **) g_malloc (from->ksize * sizeof (to->ktypevec[0]));

  for (i = 0; i < from->numk; i++)
    {
      int len = strlen (from->ktypevec[i]) + 1;

      to->ktypevec[i] = g_malloc (len);
      memcpy (to->ktypevec[i], from->ktypevec[i], len);
    }

  if (from->bsize)
    to->btypevec
      = (char **) g_malloc (from->bsize * sizeof (to->btypevec[0]));

  for (i = 0; i < from->numb; i++)
    {
      int len = strlen (from->btypevec[i]) + 1;

      to->btypevec[i] = g_malloc (len);
      memcpy (to->btypevec[i], from->btypevec[i], len);
    }

  if (from->ntmpl_args)
    to->tmpl_argvec
      = (char **) g_malloc (from->ntmpl_args * sizeof (to->tmpl_argvec[0]));

  for (i = 0; i < from->ntmpl_args; i++)
    {
      int len = strlen (from->tmpl_argvec[i]) + 1;

      to->tmpl_argvec[i] = g_malloc (len);
      memcpy (to->tmpl_argvec[i], from->tmpl_argvec[i], len);
    }

  if (from->previous_argument)
    {
      to->previous_argument = (string*) g_malloc (sizeof (string));
      string_init (to->previous_argument);
      string_appends (to->previous_argument, from->previous_argument);
    }
}


/* Delete dynamic stuff in work_stuff that is not to be re-used.  */

static void
delete_non_B_K_work_stuff (work)
     struct work_stuff *work;
{
  /* Discard the remembered types, if any.  */

  forget_types (work);
  if (work -> typevec != NULL)
    {
      g_free ((char *) work -> typevec);
      work -> typevec = NULL;
      work -> typevec_size = 0;
    }
  if (work->tmpl_argvec)
    {
      int i;

      for (i = 0; i < work->ntmpl_args; i++)
	if (work->tmpl_argvec[i])
	  g_free ((char*) work->tmpl_argvec[i]);

      g_free ((char*) work->tmpl_argvec);
      work->tmpl_argvec = NULL;
    }
  if (work->previous_argument)
    {
      string_delete (work->previous_argument);
      g_free ((char*) work->previous_argument);
      work->previous_argument = NULL;
    }
}


/* Delete all dynamic storage in work_stuff.  */
static void
delete_work_stuff (work)
     struct work_stuff *work;
{
  delete_non_B_K_work_stuff (work);
  squangle_mop_up (work);
}


/* Clear out any mangled storage */

static char *
mop_up (work, declp, success)
     struct work_stuff *work;
     string *declp;
     int success;
{
  char *demangled = NULL;

  delete_non_B_K_work_stuff (work);

  /* If demangling was successful, ensure that the demangled string is null
     terminated and return it.  Otherwise, free the demangling decl.  */

  if (!success)
    {
      string_delete (declp);
    }
  else
    {
      string_appendn (declp, "", 1);
      demangled = declp->b;
    }
  return (demangled);
}

/*

LOCAL FUNCTION

	demangle_signature -- demangle the signature part of a mangled name

SYNOPSIS

	static int
	demangle_signature (struct work_stuff *work, const char **mangled,
			    string *declp);

DESCRIPTION

	Consume and demangle the signature portion of the mangled name.

	DECLP is the string where demangled output is being built.  At
	entry it contains the demangled root name from the mangled name
	prefix.  I.E. either a demangled operator name or the root function
	name.  In some special cases, it may contain nothing.

	*MANGLED points to the current unconsumed location in the mangled
	name.  As tokens are consumed and demangling is performed, the
	pointer is updated to continuously point at the next token to
	be consumed.

	Demangling GNU style mangled names is nasty because there is no
	explicit token that marks the start of the outermost function
	argument list.  */

static int
demangle_signature (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  int success = 1;
  int func_done = 0;
  int expect_func = 0;
  int expect_return_type = 0;
  const char *oldmangled = NULL;
  string trawname;
  string tname;

  while (success && (**mangled != '\0'))
    {
      switch (**mangled)
	{
	case 'Q':
	  oldmangled = *mangled;
	  success = demangle_qualified (work, mangled, declp, 1, 0);
	  if (success)
	    remember_type (work, oldmangled, *mangled - oldmangled);
	  if (AUTO_DEMANGLING || GNU_DEMANGLING)
	    expect_func = 1;
	  oldmangled = NULL;
	  break;

        case 'K':
	  oldmangled = *mangled;
	  success = demangle_qualified (work, mangled, declp, 1, 0);
	  if (AUTO_DEMANGLING || GNU_DEMANGLING)
	    {
	      expect_func = 1;
	    }
	  oldmangled = NULL;
	  break;

	case 'S':
	  /* Static member function */
	  if (oldmangled == NULL)
	    {
	      oldmangled = *mangled;
	    }
	  (*mangled)++;
	  work -> static_type = 1;
	  break;

	case 'C':
	case 'V':
	case 'u':
	  work->type_quals |= code_for_qualifier (**mangled);

	  /* a qualified member function */
	  if (oldmangled == NULL)
	    oldmangled = *mangled;
	  (*mangled)++;
	  break;

	case 'L':
	  /* Local class name follows after "Lnnn_" */
	  if (HP_DEMANGLING)
	    {
	      while (**mangled && (**mangled != '_'))
		(*mangled)++;
	      if (!**mangled)
		success = 0;
	      else
		(*mangled)++;
	    }
	  else
	    success = 0;
	  break;

	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
	  if (oldmangled == NULL)
	    {
	      oldmangled = *mangled;
	    }
          work->temp_start = -1; /* uppermost call to demangle_class */
	  success = demangle_class (work, mangled, declp);
	  if (success)
	    {
	      remember_type (work, oldmangled, *mangled - oldmangled);
	    }
	  if (AUTO_DEMANGLING || GNU_DEMANGLING || EDG_DEMANGLING)
	    {
              /* EDG and others will have the "F", so we let the loop cycle
                 if we are looking at one. */
              if (**mangled != 'F')
                 expect_func = 1;
	    }
	  oldmangled = NULL;
	  break;

	case 'B':
	  {
	    string s;
	    success = do_type (work, mangled, &s);
	    if (success)
	      {
		string_append (&s, SCOPE_STRING (work));
		string_prepends (declp, &s);
		string_delete (&s);
	      }
	    oldmangled = NULL;
	    expect_func = 1;
	  }
	  break;

	case 'F':
	  /* Function */
	  /* ARM/HP style demangling includes a specific 'F' character after
	     the class name.  For GNU style, it is just implied.  So we can
	     safely just consume any 'F' at this point and be compatible
	     with either style.  */

	  oldmangled = NULL;
	  func_done = 1;
	  (*mangled)++;

	  /* For lucid/ARM/HP style we have to forget any types we might
	     have remembered up to this point, since they were not argument
	     types.  GNU style considers all types seen as available for
	     back references.  See comment in demangle_args() */

	  if (LUCID_DEMANGLING || ARM_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING)
	    {
	      forget_types (work);
	    }
	  success = demangle_args (work, mangled, declp);
	  /* After picking off the function args, we expect to either
	     find the function return type (preceded by an '_') or the
	     end of the string. */
	  if (success && (AUTO_DEMANGLING || EDG_DEMANGLING) && **mangled == '_')
	    {
	      ++(*mangled);
              /* At this level, we do not care about the return type. */
              success = do_type (work, mangled, &tname);
              string_delete (&tname);
            }

	  break;

	case 't':
	  /* G++ Template */
	  string_init(&trawname);
	  string_init(&tname);
	  if (oldmangled == NULL)
	    {
	      oldmangled = *mangled;
	    }
	  success = demangle_template (work, mangled, &tname,
				       &trawname, 1, 1);
	  if (success)
	    {
	      remember_type (work, oldmangled, *mangled - oldmangled);
	    }
	  string_append (&tname, SCOPE_STRING (work));

	  string_prepends(declp, &tname);
	  if (work -> destructor & 1)
	    {
	      string_prepend (&trawname, "~");
	      string_appends (declp, &trawname);
	      work->destructor -= 1;
	    }
	  if ((work->constructor & 1) || (work->destructor & 1))
	    {
	      string_appends (declp, &trawname);
	      work->constructor -= 1;
	    }
	  string_delete(&trawname);
	  string_delete(&tname);
	  oldmangled = NULL;
	  expect_func = 1;
	  break;

	case '_':
	  if ((AUTO_DEMANGLING || GNU_DEMANGLING) && expect_return_type)
	    {
	      /* Read the return type. */
	      string return_type;

	      (*mangled)++;
	      success = do_type (work, mangled, &return_type);
	      APPEND_BLANK (&return_type);

	      string_prepends (declp, &return_type);
	      string_delete (&return_type);
	      break;
	    }
	  else
	    /* At the outermost level, we cannot have a return type specified,
	       so if we run into another '_' at this point we are dealing with
	       a mangled name that is either bogus, or has been mangled by
	       some algorithm we don't know how to deal with.  So just
	       reject the entire demangling.  */
            /* However, "_nnn" is an expected suffix for alternate entry point
               numbered nnn for a function, with HP aCC, so skip over that
               without reporting failure. pai/1997-09-04 */
            if (HP_DEMANGLING)
              {
                (*mangled)++;
                while (**mangled && g_ascii_isdigit ((unsigned char)**mangled))
                  (*mangled)++;
              }
            else
	      success = 0;
	  break;

	case 'H':
	  if (AUTO_DEMANGLING || GNU_DEMANGLING)
	    {
	      /* A G++ template function.  Read the template arguments. */
	      success = demangle_template (work, mangled, declp, 0, 0,
					   0);
	      if (!(work->constructor & 1))
		expect_return_type = 1;
	      (*mangled)++;
	      break;
	    }
	  else
	    /* fall through */
	    {;}

	default:
	  if (AUTO_DEMANGLING || GNU_DEMANGLING)
	    {
	      /* Assume we have stumbled onto the first outermost function
		 argument token, and start processing args.  */
	      func_done = 1;
	      success = demangle_args (work, mangled, declp);
	    }
	  else
	    {
	      /* Non-GNU demanglers use a specific token to mark the start
		 of the outermost function argument tokens.  Typically 'F',
		 for ARM/HP-demangling, for example.  So if we find something
		 we are not prepared for, it must be an error.  */
	      success = 0;
	    }
	  break;
	}
      /*
	if (AUTO_DEMANGLING || GNU_DEMANGLING)
	*/
      {
	if (success && expect_func)
	  {
	    func_done = 1;
              if (LUCID_DEMANGLING || ARM_DEMANGLING || EDG_DEMANGLING)
                {
                  forget_types (work);
                }
	    success = demangle_args (work, mangled, declp);
	    /* Since template include the mangling of their return types,
	       we must set expect_func to 0 so that we don't try do
	       demangle more arguments the next time we get here.  */
	    expect_func = 0;
	  }
      }
    }
  if (success && !func_done)
    {
      if (AUTO_DEMANGLING || GNU_DEMANGLING)
	{
	  /* With GNU style demangling, bar__3foo is 'foo::bar(void)', and
	     bar__3fooi is 'foo::bar(int)'.  We get here when we find the
	     first case, and need to ensure that the '(void)' gets added to
	     the current declp.  Note that with ARM/HP, the first case
	     represents the name of a static data member 'foo::bar',
	     which is in the current declp, so we leave it alone.  */
	  success = demangle_args (work, mangled, declp);
	}
    }
  if (success && PRINT_ARG_TYPES)
    {
      if (work->static_type)
	string_append (declp, " static");
      if (work->type_quals != TYPE_UNQUALIFIED)
	{
	  APPEND_BLANK (declp);
	  string_append (declp, qualifier_string (work->type_quals));
	}
    }

  return (success);
}

#if 0

static int
demangle_method_args (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  int success = 0;

  if (work -> static_type)
    {
      string_append (declp, *mangled + 1);
      *mangled += strlen (*mangled);
      success = 1;
    }
  else
    {
      success = demangle_args (work, mangled, declp);
    }
  return (success);
}

#endif

static int
demangle_template_template_parm (work, mangled, tname)
     struct work_stuff *work;
     const char **mangled;
     string *tname;
{
  int i;
  int r;
  int need_comma = 0;
  int success = 1;
  string temp;

  string_append (tname, "template <");
  /* get size of template parameter list */
  if (get_count (mangled, &r))
    {
      for (i = 0; i < r; i++)
	{
	  if (need_comma)
	    {
	      string_append (tname, ", ");
	    }

	    /* Z for type parameters */
	    if (**mangled == 'Z')
	      {
		(*mangled)++;
		string_append (tname, "class");
	      }
	      /* z for template parameters */
	    else if (**mangled == 'z')
	      {
		(*mangled)++;
		success =
		  demangle_template_template_parm (work, mangled, tname);
		if (!success)
		  {
		    break;
		  }
	      }
	    else
	      {
		/* temp is initialized in do_type */
		success = do_type (work, mangled, &temp);
		if (success)
		  {
		    string_appends (tname, &temp);
		  }
		string_delete(&temp);
		if (!success)
		  {
		    break;
		  }
	      }
	  need_comma = 1;
	}

    }
  if (tname->p[-1] == '>')
    string_append (tname, " ");
  string_append (tname, "> class");
  return (success);
}

static int
demangle_expression (work, mangled, s, tk)
     struct work_stuff *work;
     const char** mangled;
     string* s;
     type_kind_t tk;
{
  int need_operator = 0;
  int success;

  success = 1;
  string_appendn (s, "(", 1);
  (*mangled)++;
  while (success && **mangled != 'W' && **mangled != '\0')
    {
      if (need_operator)
	{
	  size_t i;
	  size_t len;

	  success = 0;

	  len = strlen (*mangled);

	  for (i = 0; i < G_N_ELEMENTS (optable); ++i)
	    {
	      size_t l = strlen (optable[i].in);

	      if (l <= len
		  && memcmp (optable[i].in, *mangled, l) == 0)
		{
		  string_appendn (s, " ", 1);
		  string_append (s, optable[i].out);
		  string_appendn (s, " ", 1);
		  success = 1;
		  (*mangled) += l;
		  break;
		}
	    }

	  if (!success)
	    break;
	}
      else
	need_operator = 1;

      success = demangle_template_value_parm (work, mangled, s, tk);
    }

  if (**mangled != 'W')
    success = 0;
  else
    {
      string_appendn (s, ")", 1);
      (*mangled)++;
    }

  return success;
}

static int
demangle_integral_value (work, mangled, s)
     struct work_stuff *work;
     const char** mangled;
     string* s;
{
  int success;

  if (**mangled == 'E')
    success = demangle_expression (work, mangled, s, tk_integral);
  else if (**mangled == 'Q' || **mangled == 'K')
    success = demangle_qualified (work, mangled, s, 0, 1);
  else
    {
      int value;

      /* By default, we let the number decide whether we shall consume an
	 underscore.  */
      int multidigit_without_leading_underscore = 0;
      int leave_following_underscore = 0;

      success = 0;

      if (**mangled == '_')
        {
	  if (mangled[0][1] == 'm')
	    {
	      /* Since consume_count_with_underscores does not handle the
		 `m'-prefix we must do it here, using consume_count and
		 adjusting underscores: we have to consume the underscore
		 matching the prepended one.  */
	      multidigit_without_leading_underscore = 1;
	      string_appendn (s, "-", 1);
	      (*mangled) += 2;
	    }
	  else
	    {
	      /* Do not consume a following underscore;
	         consume_count_with_underscores will consume what
	         should be consumed.  */
	      leave_following_underscore = 1;
	    }
	}
      else
	{
	  /* Negative numbers are indicated with a leading `m'.  */
	  if (**mangled == 'm')
	  {
	    string_appendn (s, "-", 1);
	    (*mangled)++;
	  }
	  /* Since consume_count_with_underscores does not handle
	     multi-digit numbers that do not start with an underscore,
	     and this number can be an integer template parameter,
	     we have to call consume_count. */
	  multidigit_without_leading_underscore = 1;
	  /* These multi-digit numbers never end on an underscore,
	     so if there is one then don't eat it. */
	  leave_following_underscore = 1;
	}

      /* We must call consume_count if we expect to remove a trailing
	 underscore, since consume_count_with_underscores expects
	 the leading underscore (that we consumed) if it is to handle
	 multi-digit numbers.  */
      if (multidigit_without_leading_underscore)
	value = consume_count (mangled);
      else
	value = consume_count_with_underscores (mangled);

      if (value != -1)
	{
	  char buf[INTBUF_SIZE];
	  sprintf (buf, "%d", value);
	  string_append (s, buf);

	  /* Numbers not otherwise delimited, might have an underscore
	     appended as a delimeter, which we should skip.

	     ??? This used to always remove a following underscore, which
	     is wrong.  If other (arbitrary) cases are followed by an
	     underscore, we need to do something more radical.  */

	  if ((value > 9 || multidigit_without_leading_underscore)
	      && ! leave_following_underscore
	      && **mangled == '_')
	    (*mangled)++;

	  /* All is well.  */
	  success = 1;
	}
      }

  return success;
}

/* Demangle the real value in MANGLED.  */

static int
demangle_real_value (work, mangled, s)
     struct work_stuff *work;
     const char **mangled;
     string* s;
{
  if (**mangled == 'E')
    return demangle_expression (work, mangled, s, tk_real);

  if (**mangled == 'm')
    {
      string_appendn (s, "-", 1);
      (*mangled)++;
    }
  while (g_ascii_isdigit ((unsigned char)**mangled))
    {
      string_appendn (s, *mangled, 1);
      (*mangled)++;
    }
  if (**mangled == '.') /* fraction */
    {
      string_appendn (s, ".", 1);
      (*mangled)++;
      while (g_ascii_isdigit ((unsigned char)**mangled))
	{
	  string_appendn (s, *mangled, 1);
	  (*mangled)++;
	}
    }
  if (**mangled == 'e') /* exponent */
    {
      string_appendn (s, "e", 1);
      (*mangled)++;
      while (g_ascii_isdigit ((unsigned char)**mangled))
	{
	  string_appendn (s, *mangled, 1);
	  (*mangled)++;
	}
    }

  return 1;
}

static int
demangle_template_value_parm (work, mangled, s, tk)
     struct work_stuff *work;
     const char **mangled;
     string* s;
     type_kind_t tk;
{
  int success = 1;

  if (**mangled == 'Y')
    {
      /* The next argument is a template parameter. */
      int idx;

      (*mangled)++;
      idx = consume_count_with_underscores (mangled);
      if (idx == -1
	  || (work->tmpl_argvec && idx >= work->ntmpl_args)
	  || consume_count_with_underscores (mangled) == -1)
	return -1;
      if (work->tmpl_argvec)
	string_append (s, work->tmpl_argvec[idx]);
      else
	string_append_template_idx (s, idx);
    }
  else if (tk == tk_integral)
    success = demangle_integral_value (work, mangled, s);
  else if (tk == tk_char)
    {
      char tmp[2];
      int val;
      if (**mangled == 'm')
	{
	  string_appendn (s, "-", 1);
	  (*mangled)++;
	}
      string_appendn (s, "'", 1);
      val = consume_count(mangled);
      if (val <= 0)
	success = 0;
      else
	{
	  tmp[0] = (char)val;
	  tmp[1] = '\0';
	  string_appendn (s, &tmp[0], 1);
	  string_appendn (s, "'", 1);
	}
    }
  else if (tk == tk_bool)
    {
      int val = consume_count (mangled);
      if (val == 0)
	string_appendn (s, "false", 5);
      else if (val == 1)
	string_appendn (s, "true", 4);
      else
	success = 0;
    }
  else if (tk == tk_real)
    success = demangle_real_value (work, mangled, s);
  else if (tk == tk_pointer || tk == tk_reference)
    {
      if (**mangled == 'Q')
	success = demangle_qualified (work, mangled, s,
				      /*isfuncname=*/0, 
				      /*append=*/1);
      else
	{
	  int symbol_len  = consume_count (mangled);
	  if (symbol_len == -1)
	    return -1;
	  if (symbol_len == 0)
	    string_appendn (s, "0", 1);
	  else
	    {
	      char *p = g_malloc (symbol_len + 1), *q;
	      strncpy (p, *mangled, symbol_len);
	      p [symbol_len] = '\0';
	      /* We use cplus_demangle here, rather than
		 internal_cplus_demangle, because the name of the entity
		 mangled here does not make use of any of the squangling
		 or type-code information we have built up thus far; it is
		 mangled independently.  */
	      q = sysprof_cplus_demangle (p, work->options);
	      if (tk == tk_pointer)
		string_appendn (s, "&", 1);
	      /* FIXME: Pointer-to-member constants should get a
		 qualifying class name here.  */
	      if (q)
		{
		  string_append (s, q);
		  g_free (q);
		}
	      else
		string_append (s, p);
	      g_free (p);
	    }
	  *mangled += symbol_len;
	}
    }

  return success;
}

/* Demangle the template name in MANGLED.  The full name of the
   template (e.g., S<int>) is placed in TNAME.  The name without the
   template parameters (e.g. S) is placed in TRAWNAME if TRAWNAME is
   non-NULL.  If IS_TYPE is nonzero, this template is a type template,
   not a function template.  If both IS_TYPE and REMEMBER are nonzero,
   the template is remembered in the list of back-referenceable
   types.  */

static int
demangle_template (work, mangled, tname, trawname, is_type, remember)
     struct work_stuff *work;
     const char **mangled;
     string *tname;
     string *trawname;
     int is_type;
     int remember;
{
  int i;
  int r;
  int need_comma = 0;
  int success = 0;
  int is_java_array = 0;
  string temp;

  (*mangled)++;
  if (is_type)
    {
      /* get template name */
      if (**mangled == 'z')
	{
	  int idx;
	  (*mangled)++;
	  (*mangled)++;

	  idx = consume_count_with_underscores (mangled);
	  if (idx == -1
	      || (work->tmpl_argvec && idx >= work->ntmpl_args)
	      || consume_count_with_underscores (mangled) == -1)
	    return (0);

	  if (work->tmpl_argvec)
	    {
	      string_append (tname, work->tmpl_argvec[idx]);
	      if (trawname)
		string_append (trawname, work->tmpl_argvec[idx]);
	    }
	  else
	    {
	      string_append_template_idx (tname, idx);
	      if (trawname)
		string_append_template_idx (trawname, idx);
	    }
	}
      else
	{
	  if ((r = consume_count (mangled)) <= 0
	      || (int) strlen (*mangled) < r)
	    {
	      return (0);
	    }
	  is_java_array = (work -> options & DMGL_JAVA)
	    && strncmp (*mangled, "JArray1Z", 8) == 0;
	  if (! is_java_array)
	    {
	      string_appendn (tname, *mangled, r);
	    }
	  if (trawname)
	    string_appendn (trawname, *mangled, r);
	  *mangled += r;
	}
    }
  if (!is_java_array)
    string_append (tname, "<");
  /* get size of template parameter list */
  if (!get_count (mangled, &r))
    {
      return (0);
    }
  if (!is_type)
    {
      /* Create an array for saving the template argument values. */
      work->tmpl_argvec = (char**) g_malloc (r * sizeof (char *));
      work->ntmpl_args = r;
      for (i = 0; i < r; i++)
	work->tmpl_argvec[i] = 0;
    }
  for (i = 0; i < r; i++)
    {
      if (need_comma)
	{
	  string_append (tname, ", ");
	}
      /* Z for type parameters */
      if (**mangled == 'Z')
	{
	  (*mangled)++;
	  /* temp is initialized in do_type */
	  success = do_type (work, mangled, &temp);
	  if (success)
	    {
	      string_appends (tname, &temp);

	      if (!is_type)
		{
		  /* Save the template argument. */
		  int len = temp.p - temp.b;
		  work->tmpl_argvec[i] = g_malloc (len + 1);
		  memcpy (work->tmpl_argvec[i], temp.b, len);
		  work->tmpl_argvec[i][len] = '\0';
		}
	    }
	  string_delete(&temp);
	  if (!success)
	    {
	      break;
	    }
	}
      /* z for template parameters */
      else if (**mangled == 'z')
	{
	  int r2;
	  (*mangled)++;
	  success = demangle_template_template_parm (work, mangled, tname);

	  if (success
	      && (r2 = consume_count (mangled)) > 0
	      && (int) strlen (*mangled) >= r2)
	    {
	      string_append (tname, " ");
	      string_appendn (tname, *mangled, r2);
	      if (!is_type)
		{
		  /* Save the template argument. */
		  int len = r2;
		  work->tmpl_argvec[i] = g_malloc (len + 1);
		  memcpy (work->tmpl_argvec[i], *mangled, len);
		  work->tmpl_argvec[i][len] = '\0';
		}
	      *mangled += r2;
	    }
	  if (!success)
	    {
	      break;
	    }
	}
      else
	{
	  string  param;
	  string* s;

	  /* otherwise, value parameter */

	  /* temp is initialized in do_type */
	  success = do_type (work, mangled, &temp);
	  string_delete(&temp);
	  if (!success)
	    break;

	  if (!is_type)
	    {
	      s = &param;
	      string_init (s);
	    }
	  else
	    s = tname;

	  success = demangle_template_value_parm (work, mangled, s,
						  (type_kind_t) success);

	  if (!success)
	    {
	      if (!is_type)
		string_delete (s);
	      success = 0;
	      break;
	    }

	  if (!is_type)
	    {
	      int len = s->p - s->b;
	      work->tmpl_argvec[i] = g_malloc (len + 1);
	      memcpy (work->tmpl_argvec[i], s->b, len);
	      work->tmpl_argvec[i][len] = '\0';

	      string_appends (tname, s);
	      string_delete (s);
	    }
	}
      need_comma = 1;
    }
  if (is_java_array)
    {
      string_append (tname, "[]");
    }
  else
    {
      if (tname->p[-1] == '>')
	string_append (tname, " ");
      string_append (tname, ">");
    }

  if (is_type && remember)
    {
      const int bindex = register_Btype (work);
      remember_Btype (work, tname->b, LEN_STRING (tname), bindex);
    }

  /*
    if (work -> static_type)
    {
    string_append (declp, *mangled + 1);
    *mangled += strlen (*mangled);
    success = 1;
    }
    else
    {
    success = demangle_args (work, mangled, declp);
    }
    }
    */
  return (success);
}

static int
arm_pt (work, mangled, n, anchor, args)
     struct work_stuff *work;
     const char *mangled;
     int n;
     const char **anchor, **args;
{
  /* Check if ARM template with "__pt__" in it ("parameterized type") */
  /* Allow HP also here, because HP's cfront compiler follows ARM to some extent */
  if ((ARM_DEMANGLING || HP_DEMANGLING) && (*anchor = strstr (mangled, "__pt__")))
    {
      int len;
      *args = *anchor + 6;
      len = consume_count (args);
      if (len == -1)
	return 0;
      if (*args + len == mangled + n && **args == '_')
	{
	  ++*args;
	  return 1;
	}
    }
  if (AUTO_DEMANGLING || EDG_DEMANGLING)
    {
      if ((*anchor = strstr (mangled, "__tm__"))
          || (*anchor = strstr (mangled, "__ps__"))
          || (*anchor = strstr (mangled, "__pt__")))
        {
          int len;
          *args = *anchor + 6;
          len = consume_count (args);
	  if (len == -1)
	    return 0;
          if (*args + len == mangled + n && **args == '_')
            {
              ++*args;
              return 1;
            }
        }
      else if ((*anchor = strstr (mangled, "__S")))
        {
 	  int len;
 	  *args = *anchor + 3;
 	  len = consume_count (args);
	  if (len == -1)
	    return 0;
 	  if (*args + len == mangled + n && **args == '_')
            {
              ++*args;
 	      return 1;
            }
        }
    }

  return 0;
}

static void
demangle_arm_hp_template (work, mangled, n, declp)
     struct work_stuff *work;
     const char **mangled;
     int n;
     string *declp;
{
  const char *p;
  const char *args;
  const char *e = *mangled + n;
  string arg;

  /* Check for HP aCC template spec: classXt1t2 where t1, t2 are
     template args */
  if (HP_DEMANGLING && ((*mangled)[n] == 'X'))
    {
      char *start_spec_args = NULL;
      int hold_options;

      /* First check for and omit template specialization pseudo-arguments,
         such as in "Spec<#1,#1.*>" */
      start_spec_args = strchr (*mangled, '<');
      if (start_spec_args && (start_spec_args - *mangled < n))
        string_appendn (declp, *mangled, start_spec_args - *mangled);
      else
        string_appendn (declp, *mangled, n);
      (*mangled) += n + 1;
      string_init (&arg);
      if (work->temp_start == -1) /* non-recursive call */
        work->temp_start = declp->p - declp->b;

      /* We want to unconditionally demangle parameter types in
	 template parameters.  */
      hold_options = work->options;
      work->options |= DMGL_PARAMS;

      string_append (declp, "<");
      while (1)
        {
          string_delete (&arg);
          switch (**mangled)
            {
              case 'T':
                /* 'T' signals a type parameter */
                (*mangled)++;
                if (!do_type (work, mangled, &arg))
                  goto hpacc_template_args_done;
                break;

              case 'U':
              case 'S':
                /* 'U' or 'S' signals an integral value */
                if (!do_hpacc_template_const_value (work, mangled, &arg))
                  goto hpacc_template_args_done;
                break;

              case 'A':
                /* 'A' signals a named constant expression (literal) */
                if (!do_hpacc_template_literal (work, mangled, &arg))
                  goto hpacc_template_args_done;
                break;

              default:
                /* Today, 1997-09-03, we have only the above types
                   of template parameters */
                /* FIXME: maybe this should fail and return null */
                goto hpacc_template_args_done;
            }
          string_appends (declp, &arg);
         /* Check if we're at the end of template args.
             0 if at end of static member of template class,
             _ if done with template args for a function */
          if ((**mangled == '\000') || (**mangled == '_'))
            break;
          else
            string_append (declp, ",");
        }
    hpacc_template_args_done:
      string_append (declp, ">");
      string_delete (&arg);
      if (**mangled == '_')
        (*mangled)++;
      work->options = hold_options;
      return;
    }
  /* ARM template? (Also handles HP cfront extensions) */
  else if (arm_pt (work, *mangled, n, &p, &args))
    {
      int hold_options;
      string type_str;

      string_init (&arg);
      string_appendn (declp, *mangled, p - *mangled);
      if (work->temp_start == -1)  /* non-recursive call */
	work->temp_start = declp->p - declp->b;

      /* We want to unconditionally demangle parameter types in
	 template parameters.  */
      hold_options = work->options;
      work->options |= DMGL_PARAMS;

      string_append (declp, "<");
      /* should do error checking here */
      while (args < e) {
	string_delete (&arg);

	/* Check for type or literal here */
	switch (*args)
	  {
	    /* HP cfront extensions to ARM for template args */
	    /* spec: Xt1Lv1 where t1 is a type, v1 is a literal value */
	    /* FIXME: We handle only numeric literals for HP cfront */
          case 'X':
            /* A typed constant value follows */
            args++;
            if (!do_type (work, &args, &type_str))
	      goto cfront_template_args_done;
            string_append (&arg, "(");
            string_appends (&arg, &type_str);
            string_delete (&type_str);
            string_append (&arg, ")");
            if (*args != 'L')
              goto cfront_template_args_done;
            args++;
            /* Now snarf a literal value following 'L' */
            if (!snarf_numeric_literal (&args, &arg))
	      goto cfront_template_args_done;
            break;

          case 'L':
            /* Snarf a literal following 'L' */
            args++;
            if (!snarf_numeric_literal (&args, &arg))
	      goto cfront_template_args_done;
            break;
          default:
            /* Not handling other HP cfront stuff */
            {
              const char* old_args = args;
              if (!do_type (work, &args, &arg))
                goto cfront_template_args_done;

              /* Fail if we didn't make any progress: prevent infinite loop. */
              if (args == old_args)
		{
		  work->options = hold_options;
		  return;
		}
            }
	  }
	string_appends (declp, &arg);
	string_append (declp, ",");
      }
    cfront_template_args_done:
      string_delete (&arg);
      if (args >= e)
	--declp->p; /* remove extra comma */
      string_append (declp, ">");
      work->options = hold_options;
    }
  else if (n>10 && strncmp (*mangled, "_GLOBAL_", 8) == 0
	   && (*mangled)[9] == 'N'
	   && (*mangled)[8] == (*mangled)[10]
	   && strchr (cplus_markers, (*mangled)[8]))
    {
      /* A member of the anonymous namespace.  */
      string_append (declp, "{anonymous}");
    }
  else
    {
      if (work->temp_start == -1) /* non-recursive call only */
	work->temp_start = 0;     /* disable in recursive calls */
      string_appendn (declp, *mangled, n);
    }
  *mangled += n;
}

/* Extract a class name, possibly a template with arguments, from the
   mangled string; qualifiers, local class indicators, etc. have
   already been dealt with */

static int
demangle_class_name (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  int n;
  int success = 0;

  n = consume_count (mangled);
  if (n == -1)
    return 0;
  if ((int) strlen (*mangled) >= n)
    {
      demangle_arm_hp_template (work, mangled, n, declp);
      success = 1;
    }

  return (success);
}

/*

LOCAL FUNCTION

	demangle_class -- demangle a mangled class sequence

SYNOPSIS

	static int
	demangle_class (struct work_stuff *work, const char **mangled,
			strint *declp)

DESCRIPTION

	DECLP points to the buffer into which demangling is being done.

	*MANGLED points to the current token to be demangled.  On input,
	it points to a mangled class (I.E. "3foo", "13verylongclass", etc.)
	On exit, it points to the next token after the mangled class on
	success, or the first unconsumed token on failure.

	If the CONSTRUCTOR or DESTRUCTOR flags are set in WORK, then
	we are demangling a constructor or destructor.  In this case
	we prepend "class::class" or "class::~class" to DECLP.

	Otherwise, we prepend "class::" to the current DECLP.

	Reset the constructor/destructor flags once they have been
	"consumed".  This allows demangle_class to be called later during
	the same demangling, to do normal class demangling.

	Returns 1 if demangling is successful, 0 otherwise.

*/

static int
demangle_class (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  int success = 0;
  int btype;
  string class_name;
  char *save_class_name_end = 0;

  string_init (&class_name);
  btype = register_Btype (work);
  if (demangle_class_name (work, mangled, &class_name))
    {
      save_class_name_end = class_name.p;
      if ((work->constructor & 1) || (work->destructor & 1))
	{
          /* adjust so we don't include template args */
          if (work->temp_start && (work->temp_start != -1))
            {
              class_name.p = class_name.b + work->temp_start;
            }
	  string_prepends (declp, &class_name);
	  if (work -> destructor & 1)
	    {
	      string_prepend (declp, "~");
              work -> destructor -= 1;
	    }
	  else
	    {
	      work -> constructor -= 1;
	    }
	}
      class_name.p = save_class_name_end;
      remember_Ktype (work, class_name.b, LEN_STRING(&class_name));
      remember_Btype (work, class_name.b, LEN_STRING(&class_name), btype);
      string_prepend (declp, SCOPE_STRING (work));
      string_prepends (declp, &class_name);
      success = 1;
    }
  string_delete (&class_name);
  return (success);
}


/* Called when there's a "__" in the mangled name, with `scan' pointing to
   the rightmost guess.

   Find the correct "__"-sequence where the function name ends and the
   signature starts, which is ambiguous with GNU mangling.
   Call demangle_signature here, so we can make sure we found the right
   one; *mangled will be consumed so caller will not make further calls to
   demangle_signature.  */

static int
iterate_demangle_function (work, mangled, declp, scan)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
     const char *scan;
{
  const char *mangle_init = *mangled;
  int success = 0;
  string decl_init;
  struct work_stuff work_init;

  if (*(scan + 2) == '\0')
    return 0;

  /* Do not iterate for some demangling modes, or if there's only one
     "__"-sequence.  This is the normal case.  */
  if (ARM_DEMANGLING || LUCID_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING
      || strstr (scan + 2, "__") == NULL)
    {
      demangle_function_name (work, mangled, declp, scan);
      return 1;
    }

  /* Save state so we can restart if the guess at the correct "__" was
     wrong.  */
  string_init (&decl_init);
  string_appends (&decl_init, declp);
  memset (&work_init, 0, sizeof work_init);
  work_stuff_copy_to_from (&work_init, work);

  /* Iterate over occurrences of __, allowing names and types to have a
     "__" sequence in them.  We must start with the first (not the last)
     occurrence, since "__" most often occur between independent mangled
     parts, hence starting at the last occurence inside a signature
     might get us a "successful" demangling of the signature.  */

  while (scan[2])
    {
      demangle_function_name (work, mangled, declp, scan);
      success = demangle_signature (work, mangled, declp);
      if (success)
	break;

      /* Reset demangle state for the next round.  */
      *mangled = mangle_init;
      string_clear (declp);
      string_appends (declp, &decl_init);
      work_stuff_copy_to_from (work, &work_init);

      /* Leave this underscore-sequence.  */
      scan += 2;

      /* Scan for the next "__" sequence.  */
      while (*scan && (scan[0] != '_' || scan[1] != '_'))
	scan++;

      /* Move to last "__" in this sequence.  */
      while (*scan && *scan == '_')
	scan++;
      scan -= 2;
    }

  /* Delete saved state.  */
  delete_work_stuff (&work_init);
  string_delete (&decl_init);

  return success;
}

/*

LOCAL FUNCTION

	demangle_prefix -- consume the mangled name prefix and find signature

SYNOPSIS

	static int
	demangle_prefix (struct work_stuff *work, const char **mangled,
			 string *declp);

DESCRIPTION

	Consume and demangle the prefix of the mangled name.
	While processing the function name root, arrange to call
	demangle_signature if the root is ambiguous.

	DECLP points to the string buffer into which demangled output is
	placed.  On entry, the buffer is empty.  On exit it contains
	the root function name, the demangled operator name, or in some
	special cases either nothing or the completely demangled result.

	MANGLED points to the current pointer into the mangled name.  As each
	token of the mangled name is consumed, it is updated.  Upon entry
	the current mangled name pointer points to the first character of
	the mangled name.  Upon exit, it should point to the first character
	of the signature if demangling was successful, or to the first
	unconsumed character if demangling of the prefix was unsuccessful.

	Returns 1 on success, 0 otherwise.
 */

static int
demangle_prefix (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  int success = 1;
  const char *scan;
  int i;

  if (strlen(*mangled) > 6
      && (strncmp(*mangled, "_imp__", 6) == 0
          || strncmp(*mangled, "__imp_", 6) == 0))
    {
      /* it's a symbol imported from a PE dynamic library. Check for both
         new style prefix _imp__ and legacy __imp_ used by older versions
	 of dlltool. */
      (*mangled) += 6;
      work->dllimported = 1;
    }
  else if (strlen(*mangled) >= 11 && strncmp(*mangled, "_GLOBAL_", 8) == 0)
    {
      char *marker = strchr (cplus_markers, (*mangled)[8]);
      if (marker != NULL && *marker == (*mangled)[10])
	{
	  if ((*mangled)[9] == 'D')
	    {
	      /* it's a GNU global destructor to be executed at program exit */
	      (*mangled) += 11;
	      work->destructor = 2;
	      if (gnu_special (work, mangled, declp))
		return success;
	    }
	  else if ((*mangled)[9] == 'I')
	    {
	      /* it's a GNU global constructor to be executed at program init */
	      (*mangled) += 11;
	      work->constructor = 2;
	      if (gnu_special (work, mangled, declp))
		return success;
	    }
	}
    }
  else if ((ARM_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING) && strncmp(*mangled, "__std__", 7) == 0)
    {
      /* it's a ARM global destructor to be executed at program exit */
      (*mangled) += 7;
      work->destructor = 2;
    }
  else if ((ARM_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING) && strncmp(*mangled, "__sti__", 7) == 0)
    {
      /* it's a ARM global constructor to be executed at program initial */
      (*mangled) += 7;
      work->constructor = 2;
    }

  /*  This block of code is a reduction in strength time optimization
      of:
      scan = strstr (*mangled, "__"); */

  {
    scan = *mangled;

    do {
      scan = strchr (scan, '_');
    } while (scan != NULL && *++scan != '_');

    if (scan != NULL) --scan;
  }

  if (scan != NULL)
    {
      /* We found a sequence of two or more '_', ensure that we start at
	 the last pair in the sequence.  */
      i = strspn (scan, "_");
      if (i > 2)
	{
	  scan += (i - 2);
	}
    }

  if (scan == NULL)
    {
      success = 0;
    }
  else if (work -> static_type)
    {
      if (!g_ascii_isdigit ((unsigned char)scan[0]) && (scan[0] != 't'))
	{
	  success = 0;
	}
    }
  else if ((scan == *mangled)
	   && (g_ascii_isdigit ((unsigned char)scan[2]) || (scan[2] == 'Q')
	       || (scan[2] == 't') || (scan[2] == 'K') || (scan[2] == 'H')))
    {
      /* The ARM says nothing about the mangling of local variables.
	 But cfront mangles local variables by prepending __<nesting_level>
	 to them. As an extension to ARM demangling we handle this case.  */
      if ((LUCID_DEMANGLING || ARM_DEMANGLING || HP_DEMANGLING)
	  && g_ascii_isdigit ((unsigned char)scan[2]))
	{
	  *mangled = scan + 2;
	  consume_count (mangled);
	  string_append (declp, *mangled);
	  *mangled += strlen (*mangled);
	  success = 1;
	}
      else
	{
	  /* A GNU style constructor starts with __[0-9Qt].  But cfront uses
	     names like __Q2_3foo3bar for nested type names.  So don't accept
	     this style of constructor for cfront demangling.  A GNU
	     style member-template constructor starts with 'H'. */
	  if (!(LUCID_DEMANGLING || ARM_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING))
	    work -> constructor += 1;
	  *mangled = scan + 2;
	}
    }
  else if (ARM_DEMANGLING && scan[2] == 'p' && scan[3] == 't')
    {
      /* Cfront-style parameterized type.  Handled later as a signature. */
      success = 1;

      /* ARM template? */
      demangle_arm_hp_template (work, mangled, strlen (*mangled), declp);
    }
  else if (EDG_DEMANGLING && ((scan[2] == 't' && scan[3] == 'm')
                              || (scan[2] == 'p' && scan[3] == 's')
                              || (scan[2] == 'p' && scan[3] == 't')))
    {
      /* EDG-style parameterized type.  Handled later as a signature. */
      success = 1;

      /* EDG template? */
      demangle_arm_hp_template (work, mangled, strlen (*mangled), declp);
    }
  else if ((scan == *mangled) && !g_ascii_isdigit ((unsigned char)scan[2])
	   && (scan[2] != 't'))
    {
      /* Mangled name starts with "__".  Skip over any leading '_' characters,
	 then find the next "__" that separates the prefix from the signature.
	 */
      if (!(ARM_DEMANGLING || LUCID_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING)
	  || (arm_special (mangled, declp) == 0))
	{
	  while (*scan == '_')
	    {
	      scan++;
	    }
	  if ((scan = strstr (scan, "__")) == NULL || (*(scan + 2) == '\0'))
	    {
	      /* No separator (I.E. "__not_mangled"), or empty signature
		 (I.E. "__not_mangled_either__") */
	      success = 0;
	    }
	  else
	    return iterate_demangle_function (work, mangled, declp, scan);
	}
    }
  else if (*(scan + 2) != '\0')
    {
      /* Mangled name does not start with "__" but does have one somewhere
	 in there with non empty stuff after it.  Looks like a global
	 function name.  Iterate over all "__":s until the right
	 one is found.  */
      return iterate_demangle_function (work, mangled, declp, scan);
    }
  else
    {
      /* Doesn't look like a mangled name */
      success = 0;
    }

  if (!success && (work->constructor == 2 || work->destructor == 2))
    {
      string_append (declp, *mangled);
      *mangled += strlen (*mangled);
      success = 1;
    }
  return (success);
}

/*

LOCAL FUNCTION

	gnu_special -- special handling of gnu mangled strings

SYNOPSIS

	static int
	gnu_special (struct work_stuff *work, const char **mangled,
		     string *declp);


DESCRIPTION

	Process some special GNU style mangling forms that don't fit
	the normal pattern.  For example:

		_$_3foo		(destructor for class foo)
		_vt$foo		(foo virtual table)
		_vt$foo$bar	(foo::bar virtual table)
		__vt_foo	(foo virtual table, new style with thunks)
		_3foo$varname	(static data member)
		_Q22rs2tu$vw	(static data member)
		__t6vector1Zii	(constructor with template)
		__thunk_4__$_7ostream (virtual function thunk)
 */

static int
gnu_special (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  int n;
  int success = 1;
  const char *p;

  if ((*mangled)[0] == '_'
      && strchr (cplus_markers, (*mangled)[1]) != NULL
      && (*mangled)[2] == '_')
    {
      /* Found a GNU style destructor, get past "_<CPLUS_MARKER>_" */
      (*mangled) += 3;
      work -> destructor += 1;
    }
  else if ((*mangled)[0] == '_'
	   && (((*mangled)[1] == '_'
		&& (*mangled)[2] == 'v'
		&& (*mangled)[3] == 't'
		&& (*mangled)[4] == '_')
	       || ((*mangled)[1] == 'v'
		   && (*mangled)[2] == 't'
		   && strchr (cplus_markers, (*mangled)[3]) != NULL)))
    {
      /* Found a GNU style virtual table, get past "_vt<CPLUS_MARKER>"
         and create the decl.  Note that we consume the entire mangled
	 input string, which means that demangle_signature has no work
	 to do.  */
      if ((*mangled)[2] == 'v')
	(*mangled) += 5; /* New style, with thunks: "__vt_" */
      else
	(*mangled) += 4; /* Old style, no thunks: "_vt<CPLUS_MARKER>" */
      while (**mangled != '\0')
	{
	  switch (**mangled)
	    {
	    case 'Q':
	    case 'K':
	      success = demangle_qualified (work, mangled, declp, 0, 1);
	      break;
	    case 't':
	      success = demangle_template (work, mangled, declp, 0, 1,
					   1);
	      break;
	    default:
	      if (g_ascii_isdigit((unsigned char)*mangled[0]))
		{
		  n = consume_count(mangled);
		  /* We may be seeing a too-large size, or else a
		     ".<digits>" indicating a static local symbol.  In
		     any case, declare victory and move on; *don't* try
		     to use n to allocate.  */
		  if (n > (int) strlen (*mangled))
		    {
		      success = 1;
		      break;
		    }
		}
	      else
		{
		  n = strcspn (*mangled, cplus_markers);
		}
	      string_appendn (declp, *mangled, n);
	      (*mangled) += n;
	    }

	  p = strpbrk (*mangled, cplus_markers);
	  if (success && ((p == NULL) || (p == *mangled)))
	    {
	      if (p != NULL)
		{
		  string_append (declp, SCOPE_STRING (work));
		  (*mangled)++;
		}
	    }
	  else
	    {
	      success = 0;
	      break;
	    }
	}
      if (success)
	string_append (declp, " virtual table");
    }
  else if ((*mangled)[0] == '_'
	   && (strchr("0123456789Qt", (*mangled)[1]) != NULL)
	   && (p = strpbrk (*mangled, cplus_markers)) != NULL)
    {
      /* static data member, "_3foo$varname" for example */
      (*mangled)++;
      switch (**mangled)
	{
	case 'Q':
	case 'K':
	  success = demangle_qualified (work, mangled, declp, 0, 1);
	  break;
	case 't':
	  success = demangle_template (work, mangled, declp, 0, 1, 1);
	  break;
	default:
	  n = consume_count (mangled);
	  if (n < 0 || n > (long) strlen (*mangled))
	    {
	      success = 0;
	      break;
	    }

	  if (n > 10 && strncmp (*mangled, "_GLOBAL_", 8) == 0
	      && (*mangled)[9] == 'N'
	      && (*mangled)[8] == (*mangled)[10]
	      && strchr (cplus_markers, (*mangled)[8]))
	    {
	      /* A member of the anonymous namespace.  There's information
		 about what identifier or filename it was keyed to, but
		 it's just there to make the mangled name unique; we just
		 step over it.  */
	      string_append (declp, "{anonymous}");
	      (*mangled) += n;

	      /* Now p points to the marker before the N, so we need to
		 update it to the first marker after what we consumed.  */
	      p = strpbrk (*mangled, cplus_markers);
	      break;
	    }

	  string_appendn (declp, *mangled, n);
	  (*mangled) += n;
	}
      if (success && (p == *mangled))
	{
	  /* Consumed everything up to the cplus_marker, append the
	     variable name.  */
	  (*mangled)++;
	  string_append (declp, SCOPE_STRING (work));
	  n = strlen (*mangled);
	  string_appendn (declp, *mangled, n);
	  (*mangled) += n;
	}
      else
	{
	  success = 0;
	}
    }
  else if (strncmp (*mangled, "__thunk_", 8) == 0)
    {
      int delta;

      (*mangled) += 8;
      delta = consume_count (mangled);
      if (delta == -1)
	success = 0;
      else
	{
	  char *method = internal_cplus_demangle (work, ++*mangled);

	  if (method)
	    {
	      char buf[50];
	      sprintf (buf, "virtual function thunk (delta:%d) for ", -delta);
	      string_append (declp, buf);
	      string_append (declp, method);
	      g_free (method);
	      n = strlen (*mangled);
	      (*mangled) += n;
	    }
	  else
	    {
	      success = 0;
	    }
	}
    }
  else if (strncmp (*mangled, "__t", 3) == 0
	   && ((*mangled)[3] == 'i' || (*mangled)[3] == 'f'))
    {
      p = (*mangled)[3] == 'i' ? " type_info node" : " type_info function";
      (*mangled) += 4;
      switch (**mangled)
	{
	case 'Q':
	case 'K':
	  success = demangle_qualified (work, mangled, declp, 0, 1);
	  break;
	case 't':
	  success = demangle_template (work, mangled, declp, 0, 1, 1);
	  break;
	default:
	  success = do_type (work, mangled, declp);
	  break;
	}
      if (success && **mangled != '\0')
	success = 0;
      if (success)
	string_append (declp, p);
    }
  else
    {
      success = 0;
    }
  return (success);
}

static void
recursively_demangle(work, mangled, result, namelength)
     struct work_stuff *work;
     const char **mangled;
     string *result;
     int namelength;
{
  char * recurse = (char *)NULL;
  char * recurse_dem = (char *)NULL;

  recurse = (char *) g_malloc (namelength + 1);
  memcpy (recurse, *mangled, namelength);
  recurse[namelength] = '\000';

  recurse_dem = sysprof_cplus_demangle (recurse, work->options);

  if (recurse_dem)
    {
      string_append (result, recurse_dem);
      g_free (recurse_dem);
    }
  else
    {
      string_appendn (result, *mangled, namelength);
    }
  g_free (recurse);
  *mangled += namelength;
}

/*

LOCAL FUNCTION

	arm_special -- special handling of ARM/lucid mangled strings

SYNOPSIS

	static int
	arm_special (const char **mangled,
		     string *declp);


DESCRIPTION

	Process some special ARM style mangling forms that don't fit
	the normal pattern.  For example:

		__vtbl__3foo		(foo virtual table)
		__vtbl__3foo__3bar	(bar::foo virtual table)

 */

static int
arm_special (mangled, declp)
     const char **mangled;
     string *declp;
{
  int n;
  int success = 1;
  const char *scan;

  if (strncmp (*mangled, ARM_VTABLE_STRING, ARM_VTABLE_STRLEN) == 0)
    {
      /* Found a ARM style virtual table, get past ARM_VTABLE_STRING
         and create the decl.  Note that we consume the entire mangled
	 input string, which means that demangle_signature has no work
	 to do.  */
      scan = *mangled + ARM_VTABLE_STRLEN;
      while (*scan != '\0')        /* first check it can be demangled */
        {
          n = consume_count (&scan);
          if (n == -1)
	    {
	      return (0);           /* no good */
	    }
          scan += n;
          if (scan[0] == '_' && scan[1] == '_')
	    {
	      scan += 2;
	    }
        }
      (*mangled) += ARM_VTABLE_STRLEN;
      while (**mangled != '\0')
	{
	  n = consume_count (mangled);
          if (n == -1
	      || n > (long) strlen (*mangled))
	    return 0;
	  string_prependn (declp, *mangled, n);
	  (*mangled) += n;
	  if ((*mangled)[0] == '_' && (*mangled)[1] == '_')
	    {
	      string_prepend (declp, "::");
	      (*mangled) += 2;
	    }
	}
      string_append (declp, " virtual table");
    }
  else
    {
      success = 0;
    }
  return (success);
}

/*

LOCAL FUNCTION

	demangle_qualified -- demangle 'Q' qualified name strings

SYNOPSIS

	static int
	demangle_qualified (struct work_stuff *, const char *mangled,
			    string *result, int isfuncname, int append);

DESCRIPTION

	Demangle a qualified name, such as "Q25Outer5Inner" which is
	the mangled form of "Outer::Inner".  The demangled output is
	prepended or appended to the result string according to the
	state of the append flag.

	If isfuncname is nonzero, then the qualified name we are building
	is going to be used as a member function name, so if it is a
	constructor or destructor function, append an appropriate
	constructor or destructor name.  I.E. for the above example,
	the result for use as a constructor is "Outer::Inner::Inner"
	and the result for use as a destructor is "Outer::Inner::~Inner".

BUGS

	Numeric conversion is ASCII dependent (FIXME).

 */

static int
demangle_qualified (work, mangled, result, isfuncname, append)
     struct work_stuff *work;
     const char **mangled;
     string *result;
     int isfuncname;
     int append;
{
  int qualifiers = 0;
  int success = 1;
  char num[2];
  string temp;
  string last_name;
  int bindex = register_Btype (work);

  /* We only make use of ISFUNCNAME if the entity is a constructor or
     destructor.  */
  isfuncname = (isfuncname
		&& ((work->constructor & 1) || (work->destructor & 1)));

  string_init (&temp);
  string_init (&last_name);

  if ((*mangled)[0] == 'K')
    {
    /* Squangling qualified name reuse */
      int idx;
      (*mangled)++;
      idx = consume_count_with_underscores (mangled);
      if (idx == -1 || idx >= work -> numk)
        success = 0;
      else
        string_append (&temp, work -> ktypevec[idx]);
    }
  else
    switch ((*mangled)[1])
    {
    case '_':
      /* GNU mangled name with more than 9 classes.  The count is preceded
	 by an underscore (to distinguish it from the <= 9 case) and followed
	 by an underscore.  */
      (*mangled)++;
      qualifiers = consume_count_with_underscores (mangled);
      if (qualifiers == -1)
	success = 0;
      break;

    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      /* The count is in a single digit.  */
      num[0] = (*mangled)[1];
      num[1] = '\0';
      qualifiers = atoi (num);

      /* If there is an underscore after the digit, skip it.  This is
	 said to be for ARM-qualified names, but the ARM makes no
	 mention of such an underscore.  Perhaps cfront uses one.  */
      if ((*mangled)[2] == '_')
	{
	  (*mangled)++;
	}
      (*mangled) += 2;
      break;

    case '0':
    default:
      success = 0;
    }

  if (!success)
    return success;

  /* Pick off the names and collect them in the temp buffer in the order
     in which they are found, separated by '::'.  */

  while (qualifiers-- > 0)
    {
      int remember_K = 1;
      string_clear (&last_name);

      if (*mangled[0] == '_')
	(*mangled)++;

      if (*mangled[0] == 't')
	{
	  /* Here we always append to TEMP since we will want to use
	     the template name without the template parameters as a
	     constructor or destructor name.  The appropriate
	     (parameter-less) value is returned by demangle_template
	     in LAST_NAME.  We do not remember the template type here,
	     in order to match the G++ mangling algorithm.  */
	  success = demangle_template(work, mangled, &temp,
				      &last_name, 1, 0);
	  if (!success)
	    break;
	}
      else if (*mangled[0] == 'K')
	{
          int idx;
          (*mangled)++;
          idx = consume_count_with_underscores (mangled);
          if (idx == -1 || idx >= work->numk)
            success = 0;
          else
            string_append (&temp, work->ktypevec[idx]);
          remember_K = 0;

	  if (!success) break;
	}
      else
	{
	  if (EDG_DEMANGLING)
            {
	      int namelength;
 	      /* Now recursively demangle the qualifier
 	       * This is necessary to deal with templates in
 	       * mangling styles like EDG */
	      namelength = consume_count (mangled);
	      if (namelength == -1)
		{
		  success = 0;
		  break;
		}
 	      recursively_demangle(work, mangled, &temp, namelength);
            }
          else
            {
              string_delete (&last_name);
              success = do_type (work, mangled, &last_name);
              if (!success)
                break;
              string_appends (&temp, &last_name);
            }
	}

      if (remember_K)
	remember_Ktype (work, temp.b, LEN_STRING (&temp));

      if (qualifiers > 0)
	string_append (&temp, SCOPE_STRING (work));
    }

  remember_Btype (work, temp.b, LEN_STRING (&temp), bindex);

  /* If we are using the result as a function name, we need to append
     the appropriate '::' separated constructor or destructor name.
     We do this here because this is the most convenient place, where
     we already have a pointer to the name and the length of the name.  */

  if (isfuncname)
    {
      string_append (&temp, SCOPE_STRING (work));
      if (work -> destructor & 1)
	string_append (&temp, "~");
      string_appends (&temp, &last_name);
    }

  /* Now either prepend the temp buffer to the result, or append it,
     depending upon the state of the append flag.  */

  if (append)
    string_appends (result, &temp);
  else
    {
      if (!STRING_EMPTY (result))
	string_append (&temp, SCOPE_STRING (work));
      string_prepends (result, &temp);
    }

  string_delete (&last_name);
  string_delete (&temp);
  return (success);
}

/*

LOCAL FUNCTION

	get_count -- convert an ascii count to integer, consuming tokens

SYNOPSIS

	static int
	get_count (const char **type, int *count)

DESCRIPTION

	Assume that *type points at a count in a mangled name; set
	*count to its value, and set *type to the next character after
	the count.  There are some weird rules in effect here.

	If *type does not point at a string of digits, return zero.

	If *type points at a string of digits followed by an
	underscore, set *count to their value as an integer, advance
	*type to point *after the underscore, and return 1.

	If *type points at a string of digits not followed by an
	underscore, consume only the first digit.  Set *count to its
	value as an integer, leave *type pointing after that digit,
	and return 1.

        The excuse for this odd behavior: in the ARM and HP demangling
        styles, a type can be followed by a repeat count of the form
        `Nxy', where:

        `x' is a single digit specifying how many additional copies
            of the type to append to the argument list, and

        `y' is one or more digits, specifying the zero-based index of
            the first repeated argument in the list.  Yes, as you're
            unmangling the name you can figure this out yourself, but
            it's there anyway.

        So, for example, in `bar__3fooFPiN51', the first argument is a
        pointer to an integer (`Pi'), and then the next five arguments
        are the same (`N5'), and the first repeat is the function's
        second argument (`1').
*/

static int
get_count (type, count)
     const char **type;
     int *count;
{
  const char *p;
  int n;

  if (!g_ascii_isdigit ((unsigned char)**type))
    return (0);
  else
    {
      *count = **type - '0';
      (*type)++;
      if (g_ascii_isdigit ((unsigned char)**type))
	{
	  p = *type;
	  n = *count;
	  do
	    {
	      n *= 10;
	      n += *p - '0';
	      p++;
	    }
	  while (g_ascii_isdigit ((unsigned char)*p));
	  if (*p == '_')
	    {
	      *type = p + 1;
	      *count = n;
	    }
	}
    }
  return (1);
}

/* RESULT will be initialised here; it will be freed on failure.  The
   value returned is really a type_kind_t.  */

static int
do_type (work, mangled, result)
     struct work_stuff *work;
     const char **mangled;
     string *result;
{
  int n;
  int done;
  int success;
  string decl;
  const char *remembered_type;
  int type_quals;
  type_kind_t tk = tk_none;

  string_init (&decl);
  string_init (result);

  done = 0;
  success = 1;
  while (success && !done)
    {
      int member;
      switch (**mangled)
	{

	  /* A pointer type */
	case 'P':
	case 'p':
	  (*mangled)++;
	  if (! (work -> options & DMGL_JAVA))
	    string_prepend (&decl, "*");
	  if (tk == tk_none)
	    tk = tk_pointer;
	  break;

	  /* A reference type */
	case 'R':
	  (*mangled)++;
	  string_prepend (&decl, "&");
	  if (tk == tk_none)
	    tk = tk_reference;
	  break;

	  /* An array */
	case 'A':
	  {
	    ++(*mangled);
	    if (!STRING_EMPTY (&decl)
		&& (decl.b[0] == '*' || decl.b[0] == '&'))
	      {
		string_prepend (&decl, "(");
		string_append (&decl, ")");
	      }
	    string_append (&decl, "[");
	    if (**mangled != '_')
	      success = demangle_template_value_parm (work, mangled, &decl,
						      tk_integral);
	    if (**mangled == '_')
	      ++(*mangled);
	    string_append (&decl, "]");
	    break;
	  }

	/* A back reference to a previously seen type */
	case 'T':
	  (*mangled)++;
	  if (!get_count (mangled, &n) || n >= work -> ntypes)
	    {
	      success = 0;
	    }
	  else
	    {
	      remembered_type = work -> typevec[n];
	      mangled = &remembered_type;
	    }
	  break;

	  /* A function */
	case 'F':
	  (*mangled)++;
	    if (!STRING_EMPTY (&decl)
		&& (decl.b[0] == '*' || decl.b[0] == '&'))
	    {
	      string_prepend (&decl, "(");
	      string_append (&decl, ")");
	    }
	  /* After picking off the function args, we expect to either find the
	     function return type (preceded by an '_') or the end of the
	     string.  */
	  if (!demangle_nested_args (work, mangled, &decl)
	      || (**mangled != '_' && **mangled != '\0'))
	    {
	      success = 0;
	      break;
	    }
	  if (success && (**mangled == '_'))
	    (*mangled)++;
	  break;

	case 'M':
	case 'O':
	  {
	    type_quals = TYPE_UNQUALIFIED;

	    member = **mangled == 'M';
	    (*mangled)++;

	    string_append (&decl, ")");

	    /* We don't need to prepend `::' for a qualified name;
	       demangle_qualified will do that for us.  */
	    if (**mangled != 'Q')
	      string_prepend (&decl, SCOPE_STRING (work));

	    if (g_ascii_isdigit ((unsigned char)**mangled))
	      {
		n = consume_count (mangled);
		if (n == -1
		    || (int) strlen (*mangled) < n)
		  {
		    success = 0;
		    break;
		  }
		string_prependn (&decl, *mangled, n);
		*mangled += n;
	      }
	    else if (**mangled == 'X' || **mangled == 'Y')
	      {
		string temp;
		do_type (work, mangled, &temp);
		string_prepends (&decl, &temp);
		string_delete (&temp);
	      }
	    else if (**mangled == 't')
	      {
		string temp;
		string_init (&temp);
		success = demangle_template (work, mangled, &temp,
					     NULL, 1, 1);
		if (success)
		  {
		    string_prependn (&decl, temp.b, temp.p - temp.b);
		    string_delete (&temp);
		  }
		else
		  break;
	      }
	    else if (**mangled == 'Q')
	      {
		success = demangle_qualified (work, mangled, &decl,
					      /*isfuncnam=*/0, 
					      /*append=*/0);
		if (!success)
		  break;
	      }
	    else
	      {
		success = 0;
		break;
	      }

	    string_prepend (&decl, "(");
	    if (member)
	      {
		switch (**mangled)
		  {
		  case 'C':
		  case 'V':
		  case 'u':
		    type_quals |= code_for_qualifier (**mangled);
		    (*mangled)++;
		    break;

		  default:
		    break;
		  }

		if (*(*mangled)++ != 'F')
		  {
		    success = 0;
		    break;
		  }
	      }
	    if ((member && !demangle_nested_args (work, mangled, &decl))
		|| **mangled != '_')
	      {
		success = 0;
		break;
	      }
	    (*mangled)++;
	    if (! PRINT_ANSI_QUALIFIERS)
	      {
		break;
	      }
	    if (type_quals != TYPE_UNQUALIFIED)
	      {
		APPEND_BLANK (&decl);
		string_append (&decl, qualifier_string (type_quals));
	      }
	    break;
	  }
        case 'G':
	  (*mangled)++;
	  break;

	case 'C':
	case 'V':
	case 'u':
	  if (PRINT_ANSI_QUALIFIERS)
	    {
	      if (!STRING_EMPTY (&decl))
		string_prepend (&decl, " ");

	      string_prepend (&decl, demangle_qualifier (**mangled));
	    }
	  (*mangled)++;
	  break;
	  /*
	    }
	    */

	  /* fall through */
	default:
	  done = 1;
	  break;
	}
    }

  if (success) switch (**mangled)
    {
      /* A qualified name, such as "Outer::Inner".  */
    case 'Q':
    case 'K':
      {
        success = demangle_qualified (work, mangled, result, 0, 1);
        break;
      }

    /* A back reference to a previously seen squangled type */
    case 'B':
      (*mangled)++;
      if (!get_count (mangled, &n) || n >= work -> numb)
	success = 0;
      else
	string_append (result, work->btypevec[n]);
      break;

    case 'X':
    case 'Y':
      /* A template parm.  We substitute the corresponding argument. */
      {
	int idx;

	(*mangled)++;
	idx = consume_count_with_underscores (mangled);

	if (idx == -1
	    || (work->tmpl_argvec && idx >= work->ntmpl_args)
	    || consume_count_with_underscores (mangled) == -1)
	  {
	    success = 0;
	    break;
	  }

	if (work->tmpl_argvec)
	  string_append (result, work->tmpl_argvec[idx]);
	else
	  string_append_template_idx (result, idx);

	success = 1;
      }
    break;

    default:
      success = demangle_fund_type (work, mangled, result);
      if (tk == tk_none)
	tk = (type_kind_t) success;
      break;
    }

  if (success)
    {
      if (!STRING_EMPTY (&decl))
	{
	  string_append (result, " ");
	  string_appends (result, &decl);
	}
    }
  else
    string_delete (result);
  string_delete (&decl);

  if (success)
    /* Assume an integral type, if we're not sure.  */
    return (int) ((tk == tk_none) ? tk_integral : tk);
  else
    return 0;
}

/* Given a pointer to a type string that represents a fundamental type
   argument (int, long, unsigned int, etc) in TYPE, a pointer to the
   string in which the demangled output is being built in RESULT, and
   the WORK structure, decode the types and add them to the result.

   For example:

   	"Ci"	=>	"const int"
	"Sl"	=>	"signed long"
	"CUs"	=>	"const unsigned short"

   The value returned is really a type_kind_t.  */

static int
demangle_fund_type (work, mangled, result)
     struct work_stuff *work;
     const char **mangled;
     string *result;
{
  int done = 0;
  int success = 1;
  char buf[10];
  unsigned int dec = 0;
  type_kind_t tk = tk_integral;

  /* First pick off any type qualifiers.  There can be more than one.  */

  while (!done)
    {
      switch (**mangled)
	{
	case 'C':
	case 'V':
	case 'u':
	  if (PRINT_ANSI_QUALIFIERS)
	    {
              if (!STRING_EMPTY (result))
                string_prepend (result, " ");
	      string_prepend (result, demangle_qualifier (**mangled));
	    }
	  (*mangled)++;
	  break;
	case 'U':
	  (*mangled)++;
	  APPEND_BLANK (result);
	  string_append (result, "unsigned");
	  break;
	case 'S': /* signed char only */
	  (*mangled)++;
	  APPEND_BLANK (result);
	  string_append (result, "signed");
	  break;
	case 'J':
	  (*mangled)++;
	  APPEND_BLANK (result);
	  string_append (result, "__complex");
	  break;
	default:
	  done = 1;
	  break;
	}
    }

  /* Now pick off the fundamental type.  There can be only one.  */

  switch (**mangled)
    {
    case '\0':
    case '_':
      break;
    case 'v':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "void");
      break;
    case 'x':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "long long");
      break;
    case 'l':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "long");
      break;
    case 'i':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "int");
      break;
    case 's':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "short");
      break;
    case 'b':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "bool");
      tk = tk_bool;
      break;
    case 'c':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "char");
      tk = tk_char;
      break;
    case 'w':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "wchar_t");
      tk = tk_char;
      break;
    case 'r':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "long double");
      tk = tk_real;
      break;
    case 'd':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "double");
      tk = tk_real;
      break;
    case 'f':
      (*mangled)++;
      APPEND_BLANK (result);
      string_append (result, "float");
      tk = tk_real;
      break;
    case 'G':
      (*mangled)++;
      if (!g_ascii_isdigit ((unsigned char)**mangled))
	{
	  success = 0;
	  break;
	}
    case 'I':
      (*mangled)++;
      if (**mangled == '_')
	{
	  int i;
	  (*mangled)++;
	  for (i = 0;
	       i < (long) sizeof (buf) - 1 && **mangled && **mangled != '_';
	       (*mangled)++, i++)
	    buf[i] = **mangled;
	  if (**mangled != '_')
	    {
	      success = 0;
	      break;
	    }
	  buf[i] = '\0';
	  (*mangled)++;
	}
      else
	{
	  strncpy (buf, *mangled, 2);
	  buf[2] = '\0';
	  *mangled += min (strlen (*mangled), 2);
	}
      sscanf (buf, "%x", &dec);
      sprintf (buf, "int%u_t", dec);
      APPEND_BLANK (result);
      string_append (result, buf);
      break;

      /* fall through */
      /* An explicit type, such as "6mytype" or "7integer" */
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      {
        int bindex = register_Btype (work);
        string btype;
        string_init (&btype);
        if (demangle_class_name (work, mangled, &btype)) {
          remember_Btype (work, btype.b, LEN_STRING (&btype), bindex);
          APPEND_BLANK (result);
          string_appends (result, &btype);
        }
        else
          success = 0;
        string_delete (&btype);
        break;
      }
    case 't':
      {
        string btype;
        string_init (&btype);
        success = demangle_template (work, mangled, &btype, 0, 1, 1);
        string_appends (result, &btype);
        string_delete (&btype);
        break;
      }
    default:
      success = 0;
      break;
    }

  return success ? ((int) tk) : 0;
}


/* Handle a template's value parameter for HP aCC (extension from ARM)
   **mangled points to 'S' or 'U' */

static int
do_hpacc_template_const_value (work, mangled, result)
     struct work_stuff *work ATTRIBUTE_UNUSED;
     const char **mangled;
     string *result;
{
  int unsigned_const;

  if (**mangled != 'U' && **mangled != 'S')
    return 0;

  unsigned_const = (**mangled == 'U');

  (*mangled)++;

  switch (**mangled)
    {
      case 'N':
        string_append (result, "-");
        /* fall through */
      case 'P':
        (*mangled)++;
        break;
      case 'M':
        /* special case for -2^31 */
        string_append (result, "-2147483648");
        (*mangled)++;
        return 1;
      default:
        return 0;
    }

  /* We have to be looking at an integer now */
  if (!(g_ascii_isdigit ((unsigned char)**mangled)))
    return 0;

  /* We only deal with integral values for template
     parameters -- so it's OK to look only for digits */
  while (g_ascii_isdigit ((unsigned char)**mangled))
    {
      char_str[0] = **mangled;
      string_append (result, char_str);
      (*mangled)++;
    }

  if (unsigned_const)
    string_append (result, "U");

  /* FIXME? Some day we may have 64-bit (or larger :-) ) constants
     with L or LL suffixes. pai/1997-09-03 */

  return 1; /* success */
}

/* Handle a template's literal parameter for HP aCC (extension from ARM)
   **mangled is pointing to the 'A' */

static int
do_hpacc_template_literal (work, mangled, result)
     struct work_stuff *work;
     const char **mangled;
     string *result;
{
  int literal_len = 0;
  char * recurse;
  char * recurse_dem;

  if (**mangled != 'A')
    return 0;

  (*mangled)++;

  literal_len = consume_count (mangled);

  if (literal_len <= 0)
    return 0;

  /* Literal parameters are names of arrays, functions, etc.  and the
     canonical representation uses the address operator */
  string_append (result, "&");

  /* Now recursively demangle the literal name */
  recurse = (char *) g_malloc (literal_len + 1);
  memcpy (recurse, *mangled, literal_len);
  recurse[literal_len] = '\000';

  recurse_dem = sysprof_cplus_demangle (recurse, work->options);

  if (recurse_dem)
    {
      string_append (result, recurse_dem);
      g_free (recurse_dem);
    }
  else
    {
      string_appendn (result, *mangled, literal_len);
    }
  (*mangled) += literal_len;
  g_free (recurse);

  return 1;
}

static int
snarf_numeric_literal (args, arg)
     const char ** args;
     string * arg;
{
  if (**args == '-')
    {
      char_str[0] = '-';
      string_append (arg, char_str);
      (*args)++;
    }
  else if (**args == '+')
    (*args)++;

  if (!g_ascii_isdigit ((unsigned char)**args))
    return 0;

  while (g_ascii_isdigit ((unsigned char)**args))
    {
      char_str[0] = **args;
      string_append (arg, char_str);
      (*args)++;
    }

  return 1;
}

/* Demangle the next argument, given by MANGLED into RESULT, which
   *should be an uninitialized* string.  It will be initialized here,
   and free'd should anything go wrong.  */

static int
do_arg (work, mangled, result)
     struct work_stuff *work;
     const char **mangled;
     string *result;
{
  /* Remember where we started so that we can record the type, for
     non-squangling type remembering.  */
  const char *start = *mangled;

  string_init (result);

  if (work->nrepeats > 0)
    {
      --work->nrepeats;

      if (work->previous_argument == 0)
	return 0;

      /* We want to reissue the previous type in this argument list.  */
      string_appends (result, work->previous_argument);
      return 1;
    }

  if (**mangled == 'n')
    {
      /* A squangling-style repeat.  */
      (*mangled)++;
      work->nrepeats = consume_count(mangled);

      if (work->nrepeats <= 0)
	/* This was not a repeat count after all.  */
	return 0;

      if (work->nrepeats > 9)
	{
	  if (**mangled != '_')
	    /* The repeat count should be followed by an '_' in this
	       case.  */
	    return 0;
	  else
	    (*mangled)++;
	}

      /* Now, the repeat is all set up.  */
      return do_arg (work, mangled, result);
    }

  /* Save the result in WORK->previous_argument so that we can find it
     if it's repeated.  Note that saving START is not good enough: we
     do not want to add additional types to the back-referenceable
     type vector when processing a repeated type.  */
  if (work->previous_argument)
    string_delete (work->previous_argument);
  else
    work->previous_argument = (string*) g_malloc (sizeof (string));

  if (!do_type (work, mangled, work->previous_argument))
    return 0;

  string_appends (result, work->previous_argument);

  remember_type (work, start, *mangled - start);
  return 1;
}

static void
remember_type (work, start, len)
     struct work_stuff *work;
     const char *start;
     int len;
{
  char *tem;

  if (work->forgetting_types)
    return;

  if (work -> ntypes >= work -> typevec_size)
    {
      if (work -> typevec_size == 0)
	{
	  work -> typevec_size = 3;
	  work -> typevec
	    = (char **) g_malloc (sizeof (char *) * work -> typevec_size);
	}
      else
	{
	  work -> typevec_size *= 2;
	  work -> typevec
	    = (char **) g_realloc ((char *)work -> typevec,
				  sizeof (char *) * work -> typevec_size);
	}
    }
  tem = g_malloc (len + 1);
  memcpy (tem, start, len);
  tem[len] = '\0';
  work -> typevec[work -> ntypes++] = tem;
}


/* Remember a K type class qualifier. */
static void
remember_Ktype (work, start, len)
     struct work_stuff *work;
     const char *start;
     int len;
{
  char *tem;

  if (work -> numk >= work -> ksize)
    {
      if (work -> ksize == 0)
	{
	  work -> ksize = 5;
	  work -> ktypevec
	    = (char **) g_malloc (sizeof (char *) * work -> ksize);
	}
      else
	{
	  work -> ksize *= 2;
	  work -> ktypevec
	    = (char **) g_realloc ((char *)work -> ktypevec,
				  sizeof (char *) * work -> ksize);
	}
    }
  tem = g_malloc (len + 1);
  memcpy (tem, start, len);
  tem[len] = '\0';
  work -> ktypevec[work -> numk++] = tem;
}

/* Register a B code, and get an index for it. B codes are registered
   as they are seen, rather than as they are completed, so map<temp<char> >
   registers map<temp<char> > as B0, and temp<char> as B1 */

static int
register_Btype (work)
     struct work_stuff *work;
{
  int ret;

  if (work -> numb >= work -> bsize)
    {
      if (work -> bsize == 0)
	{
	  work -> bsize = 5;
	  work -> btypevec
	    = (char **) g_malloc (sizeof (char *) * work -> bsize);
	}
      else
	{
	  work -> bsize *= 2;
	  work -> btypevec
	    = (char **) g_realloc ((char *)work -> btypevec,
				  sizeof (char *) * work -> bsize);
	}
    }
  ret = work -> numb++;
  work -> btypevec[ret] = NULL;
  return(ret);
}

/* Store a value into a previously registered B code type. */

static void
remember_Btype (work, start, len, index)
     struct work_stuff *work;
     const char *start;
     int len, index;
{
  char *tem;

  tem = g_malloc (len + 1);
  memcpy (tem, start, len);
  tem[len] = '\0';
  work -> btypevec[index] = tem;
}

/* Lose all the info related to B and K type codes. */
static void
forget_B_and_K_types (work)
     struct work_stuff *work;
{
  int i;

  while (work -> numk > 0)
    {
      i = --(work -> numk);
      if (work -> ktypevec[i] != NULL)
	{
	  g_free (work -> ktypevec[i]);
	  work -> ktypevec[i] = NULL;
	}
    }

  while (work -> numb > 0)
    {
      i = --(work -> numb);
      if (work -> btypevec[i] != NULL)
	{
	  g_free (work -> btypevec[i]);
	  work -> btypevec[i] = NULL;
	}
    }
}
/* Forget the remembered types, but not the type vector itself.  */

static void
forget_types (work)
     struct work_stuff *work;
{
  int i;

  while (work -> ntypes > 0)
    {
      i = --(work -> ntypes);
      if (work -> typevec[i] != NULL)
	{
	  g_free (work -> typevec[i]);
	  work -> typevec[i] = NULL;
	}
    }
}

/* Process the argument list part of the signature, after any class spec
   has been consumed, as well as the first 'F' character (if any).  For
   example:

   "__als__3fooRT0"		=>	process "RT0"
   "complexfunc5__FPFPc_PFl_i"	=>	process "PFPc_PFl_i"

   DECLP must be already initialised, usually non-empty.  It won't be freed
   on failure.

   Note that g++ differs significantly from ARM and lucid style mangling
   with regards to references to previously seen types.  For example, given
   the source fragment:

     class foo {
       public:
       foo::foo (int, foo &ia, int, foo &ib, int, foo &ic);
     };

     foo::foo (int, foo &ia, int, foo &ib, int, foo &ic) { ia = ib = ic; }
     void foo (int, foo &ia, int, foo &ib, int, foo &ic) { ia = ib = ic; }

   g++ produces the names:

     __3fooiRT0iT2iT2
     foo__FiR3fooiT1iT1

   while lcc (and presumably other ARM style compilers as well) produces:

     foo__FiR3fooT1T2T1T2
     __ct__3fooFiR3fooT1T2T1T2

   Note that g++ bases its type numbers starting at zero and counts all
   previously seen types, while lucid/ARM bases its type numbers starting
   at one and only considers types after it has seen the 'F' character
   indicating the start of the function args.  For lucid/ARM style, we
   account for this difference by discarding any previously seen types when
   we see the 'F' character, and subtracting one from the type number
   reference.

 */

static int
demangle_args (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  string arg;
  int need_comma = 0;
  int r;
  int t;
  const char *tem;
  char temptype;

  if (PRINT_ARG_TYPES)
    {
      string_append (declp, "(");
      if (**mangled == '\0')
	{
	  string_append (declp, "void");
	}
    }

  while ((**mangled != '_' && **mangled != '\0' && **mangled != 'e')
	 || work->nrepeats > 0)
    {
      if ((**mangled == 'N') || (**mangled == 'T'))
	{
	  temptype = *(*mangled)++;

	  if (temptype == 'N')
	    {
	      if (!get_count (mangled, &r))
		{
		  return (0);
		}
	    }
	  else
	    {
	      r = 1;
	    }
          if ((HP_DEMANGLING || ARM_DEMANGLING || EDG_DEMANGLING) && work -> ntypes >= 10)
            {
              /* If we have 10 or more types we might have more than a 1 digit
                 index so we'll have to consume the whole count here. This
                 will lose if the next thing is a type name preceded by a
                 count but it's impossible to demangle that case properly
                 anyway. Eg if we already have 12 types is T12Pc "(..., type1,
                 Pc, ...)"  or "(..., type12, char *, ...)" */
              if ((t = consume_count(mangled)) <= 0)
                {
                  return (0);
                }
            }
          else
	    {
	      if (!get_count (mangled, &t))
	    	{
	          return (0);
	    	}
	    }
	  if (LUCID_DEMANGLING || ARM_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING)
	    {
	      t--;
	    }
	  /* Validate the type index.  Protect against illegal indices from
	     malformed type strings.  */
	  if ((t < 0) || (t >= work -> ntypes))
	    {
	      return (0);
	    }
	  while (work->nrepeats > 0 || --r >= 0)
	    {
	      tem = work -> typevec[t];
	      if (need_comma && PRINT_ARG_TYPES)
		{
		  string_append (declp, ", ");
		}
	      if (!do_arg (work, &tem, &arg))
		{
		  return (0);
		}
	      if (PRINT_ARG_TYPES)
		{
		  string_appends (declp, &arg);
		}
	      string_delete (&arg);
	      need_comma = 1;
	    }
	}
      else
	{
	  if (need_comma && PRINT_ARG_TYPES)
	    string_append (declp, ", ");
	  if (!do_arg (work, mangled, &arg))
	    return (0);
	  if (PRINT_ARG_TYPES)
	    string_appends (declp, &arg);
	  string_delete (&arg);
	  need_comma = 1;
	}
    }

  if (**mangled == 'e')
    {
      (*mangled)++;
      if (PRINT_ARG_TYPES)
	{
	  if (need_comma)
	    {
	      string_append (declp, ",");
	    }
	  string_append (declp, "...");
	}
    }

  if (PRINT_ARG_TYPES)
    {
      string_append (declp, ")");
    }
  return (1);
}

/* Like demangle_args, but for demangling the argument lists of function
   and method pointers or references, not top-level declarations.  */

static int
demangle_nested_args (work, mangled, declp)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
{
  string* saved_previous_argument;
  int result;
  int saved_nrepeats;

  /* The G++ name-mangling algorithm does not remember types on nested
     argument lists, unless -fsquangling is used, and in that case the
     type vector updated by remember_type is not used.  So, we turn
     off remembering of types here.  */
  ++work->forgetting_types;

  /* For the repeat codes used with -fsquangling, we must keep track of
     the last argument.  */
  saved_previous_argument = work->previous_argument;
  saved_nrepeats = work->nrepeats;
  work->previous_argument = 0;
  work->nrepeats = 0;

  /* Actually demangle the arguments.  */
  result = demangle_args (work, mangled, declp);

  /* Restore the previous_argument field.  */
  if (work->previous_argument)
    {
      string_delete (work->previous_argument);
      g_free ((char *) work->previous_argument);
    }
  work->previous_argument = saved_previous_argument;
  --work->forgetting_types;
  work->nrepeats = saved_nrepeats;

  return result;
}

static void
demangle_function_name (work, mangled, declp, scan)
     struct work_stuff *work;
     const char **mangled;
     string *declp;
     const char *scan;
{
  size_t i;
  string type;
  const char *tem;

  string_appendn (declp, (*mangled), scan - (*mangled));
  string_need (declp, 1);
  *(declp -> p) = '\0';

  /* Consume the function name, including the "__" separating the name
     from the signature.  We are guaranteed that SCAN points to the
     separator.  */

  (*mangled) = scan + 2;
  /* We may be looking at an instantiation of a template function:
     foo__Xt1t2_Ft3t4, where t1, t2, ... are template arguments and a
     following _F marks the start of the function arguments.  Handle
     the template arguments first. */

  if (HP_DEMANGLING && (**mangled == 'X'))
    {
      demangle_arm_hp_template (work, mangled, 0, declp);
      /* This leaves MANGLED pointing to the 'F' marking func args */
    }

  if (LUCID_DEMANGLING || ARM_DEMANGLING || HP_DEMANGLING || EDG_DEMANGLING)
    {

      /* See if we have an ARM style constructor or destructor operator.
	 If so, then just record it, clear the decl, and return.
	 We can't build the actual constructor/destructor decl until later,
	 when we recover the class name from the signature.  */

      if (strcmp (declp -> b, "__ct") == 0)
	{
	  work -> constructor += 1;
	  string_clear (declp);
	  return;
	}
      else if (strcmp (declp -> b, "__dt") == 0)
	{
	  work -> destructor += 1;
	  string_clear (declp);
	  return;
	}
    }

  if (declp->p - declp->b >= 3
      && declp->b[0] == 'o'
      && declp->b[1] == 'p'
      && strchr (cplus_markers, declp->b[2]) != NULL)
    {
      /* see if it's an assignment expression */
      if (declp->p - declp->b >= 10 /* op$assign_ */
	  && memcmp (declp->b + 3, "assign_", 7) == 0)
	{
	  for (i = 0; i < G_N_ELEMENTS (optable); i++)
	    {
	      int len = declp->p - declp->b - 10;
	      if ((int) strlen (optable[i].in) == len
		  && memcmp (optable[i].in, declp->b + 10, len) == 0)
		{
		  string_clear (declp);
		  string_append (declp, "operator");
		  string_append (declp, optable[i].out);
		  string_append (declp, "=");
		  break;
		}
	    }
	}
      else
	{
	  for (i = 0; i < G_N_ELEMENTS (optable); i++)
	    {
	      int len = declp->p - declp->b - 3;
	      if ((int) strlen (optable[i].in) == len
		  && memcmp (optable[i].in, declp->b + 3, len) == 0)
		{
		  string_clear (declp);
		  string_append (declp, "operator");
		  string_append (declp, optable[i].out);
		  break;
		}
	    }
	}
    }
  else if (declp->p - declp->b >= 5 && memcmp (declp->b, "type", 4) == 0
	   && strchr (cplus_markers, declp->b[4]) != NULL)
    {
      /* type conversion operator */
      tem = declp->b + 5;
      if (do_type (work, &tem, &type))
	{
	  string_clear (declp);
	  string_append (declp, "operator ");
	  string_appends (declp, &type);
	  string_delete (&type);
	}
    }
  else if (declp->b[0] == '_' && declp->b[1] == '_'
	   && declp->b[2] == 'o' && declp->b[3] == 'p')
    {
      /* ANSI.  */
      /* type conversion operator.  */
      tem = declp->b + 4;
      if (do_type (work, &tem, &type))
	{
	  string_clear (declp);
	  string_append (declp, "operator ");
	  string_appends (declp, &type);
	  string_delete (&type);
	}
    }
  else if (declp->b[0] == '_' && declp->b[1] == '_'
	   && g_ascii_islower((unsigned char)declp->b[2])
	   && g_ascii_islower((unsigned char)declp->b[3]))
    {
      if (declp->b[4] == '\0')
	{
	  /* Operator.  */
	  for (i = 0; i < G_N_ELEMENTS (optable); i++)
	    {
	      if (strlen (optable[i].in) == 2
		  && memcmp (optable[i].in, declp->b + 2, 2) == 0)
		{
		  string_clear (declp);
		  string_append (declp, "operator");
		  string_append (declp, optable[i].out);
		  break;
		}
	    }
	}
      else
	{
	  if (declp->b[2] == 'a' && declp->b[5] == '\0')
	    {
	      /* Assignment.  */
	      for (i = 0; i < G_N_ELEMENTS (optable); i++)
		{
		  if (strlen (optable[i].in) == 3
		      && memcmp (optable[i].in, declp->b + 2, 3) == 0)
		    {
		      string_clear (declp);
		      string_append (declp, "operator");
		      string_append (declp, optable[i].out);
		      break;
		    }
		}
	    }
	}
    }
}

/* a mini string-handling package */

static void
string_need (s, n)
     string *s;
     int n;
{
  int tem;

  if (s->b == NULL)
    {
      if (n < 32)
	{
	  n = 32;
	}
      s->p = s->b = g_malloc (n);
      s->e = s->b + n;
    }
  else if (s->e - s->p < n)
    {
      tem = s->p - s->b;
      n += tem;
      n *= 2;
      s->b = g_realloc (s->b, n);
      s->p = s->b + tem;
      s->e = s->b + n;
    }
}

static void
string_delete (s)
     string *s;
{
  if (s->b != NULL)
    {
      g_free (s->b);
      s->b = s->e = s->p = NULL;
    }
}

static void
string_init (s)
     string *s;
{
  s->b = s->p = s->e = NULL;
}

static void
string_clear (s)
     string *s;
{
  s->p = s->b;
}

#if 0

static int
string_empty (s)
     string *s;
{
  return (s->b == s->p);
}

#endif

static void
string_append (p, s)
     string *p;
     const char *s;
{
  int n;
  if (s == NULL || *s == '\0')
    return;
  n = strlen (s);
  string_need (p, n);
  memcpy (p->p, s, n);
  p->p += n;
}

static void
string_appends (p, s)
     string *p, *s;
{
  int n;

  if (s->b != s->p)
    {
      n = s->p - s->b;
      string_need (p, n);
      memcpy (p->p, s->b, n);
      p->p += n;
    }
}

static void
string_appendn (p, s, n)
     string *p;
     const char *s;
     int n;
{
  if (n != 0)
    {
      string_need (p, n);
      memcpy (p->p, s, n);
      p->p += n;
    }
}

static void
string_prepend (p, s)
     string *p;
     const char *s;
{
  if (s != NULL && *s != '\0')
    {
      string_prependn (p, s, strlen (s));
    }
}

static void
string_prepends (p, s)
     string *p, *s;
{
  if (s->b != s->p)
    {
      string_prependn (p, s->b, s->p - s->b);
    }
}

static void
string_prependn (p, s, n)
     string *p;
     const char *s;
     int n;
{
  char *q;

  if (n != 0)
    {
      string_need (p, n);
      for (q = p->p - 1; q >= p->b; q--)
	{
	  q[n] = q[0];
	}
      memcpy (p->b, s, n);
      p->p += n;
    }
}

static void
string_append_template_idx (s, idx)
     string *s;
     int idx;
{
  char buf[INTBUF_SIZE + 1 /* 'T' */];
  sprintf(buf, "T%d", idx);
  string_append (s, buf);
}
