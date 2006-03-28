/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, 2006, Soeren Sandmann
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef SFORMAT_H
#define SFORMAT_H

typedef struct SFormat SFormat;
typedef struct SType SType;
typedef struct SForward SForward;
typedef struct SContext SContext;

SFormat *sformat_new (void);
void     sformat_free (SFormat *format);
void	 sformat_set_type (SFormat *format,
			   SType   *type);
SForward *sformat_declare_forward (SFormat *format);
SType   *sformat_make_record (SFormat *format,
			      const char *name,
			      SForward *forward,
			      SType *content,
			      ...);
SType	*sformat_make_list   (SFormat *format,
			      const char *name,
			      SForward   *forward,
			      SType   *type);
SType   *sformat_make_pointer (SFormat *format,
			       const char *name,
			       SForward *type);
SType   *sformat_make_integer (SFormat *format,
			       const    char *name);
SType   *sformat_make_string  (SFormat *format,
			       const    char *name);

gboolean stype_is_record (SType *type);
gboolean stype_is_list   (SType *type);
gboolean stype_is_pointer (SType *type);
gboolean stype_is_integer (SType *type);
gboolean stype_is_string (SType *type);
SType *stype_get_target_type (SType *type);
const char *stype_get_name (SType *type);

SContext *scontext_new (SFormat *format);
SType *scontext_begin (SContext *context,
		       const char *element);
SType *scontext_text (SContext *context);
SType *scontext_end (SContext *context,
		     const char *element);
gboolean scontext_is_finished (SContext *context);
void scontext_free (SContext *context);

#endif
