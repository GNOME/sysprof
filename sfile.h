/* Sysprof -- Sampling, systemwide CPU profiler
 * Copyright 2004, Red Hat, Inc.
 * Copyright 2004, 2005, Soeren Sandmann
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

typedef struct SFormat SFormat;
typedef struct SFileInput SFileInput;
typedef struct SFileOutput SFileOutput;
typedef guint SType;


/* A possibly better API/naming scheme
 *
 * Serializer *serializer_new (SerializerFormat *format);
 *
 * SerializerReadContext *serializer_begin_read_filename (serializer *serialize,
 *						          const char *filename,
 *                                                        GError *err);
 * serializer_get_blah (SerializerReadContext *);
 * void serialzier_end_read (...);
 *
 * SerializerWritecontext *...;
 * serializer_begin_write (context);
 * serializer_write_int ();
 * serializer_end_write (..., GError **err);
 *
 *
 *  For formats consider:
 *
 *   Format *format_new (void);
 *   void format_set_record (Format *f, Content *r);
 *   Content *new_record (Content *c1, ...);
 *
 *   List *format_new_list (Format *f
 *   
 *
 * Consider adding optional elements:
 *
 *         sformat_new_optional (gpointer content)
 *
 * enums, optionals, selections, empties
 *
 *
 *==============================================
 * Also think about versioning - apps will want to be able to read and write
 * different versions of the format, and they want to be able to sniff the
 * format + version
 *
 */

/* - Describing Types - */
SFormat *sformat_new         (gpointer    f);
gpointer sformat_new_record  (const char *name,
			      SType      *type,
			      gpointer    content,
			      ...);
gpointer sformat_new_list    (const char *name,
			      SType      *type,
			      gpointer    content);
gpointer sformat_new_pointer (const char *name,
			      SType      *target_type);
gpointer sformat_new_integer (const char *name);
gpointer sformat_new_string  (const char *name);
void     sformat_free        (SFormat    *format);


/* - Reading - */
SFileInput *  sfile_load        (const char  *filename,
				 SFormat       *format,
				 GError     **err);
void     sfile_begin_get_record (SFileInput  *file,
				 const char *name);
int      sfile_begin_get_list   (SFileInput  *file,
				 const char *name);
void     sfile_get_pointer      (SFileInput  *file,
				 const char *name,
				 gpointer    *pointer);
void	 sfile_get_integer      (SFileInput  *file,
				 const char *name,
				 gint32     *integer);
void     sfile_get_string       (SFileInput  *file,
				 const char *name,
				 char       **string);
void     sfile_end_get          (SFileInput  *file,
				 const char *name,
				 gpointer     object);
void	 sfile_input_free	(SFileInput  *file);

#if 0
/* incremental loading (worth considering at least) */
SFileLoader *sfile_loader_new      (SFormat        *format);
void         sfile_loader_add_text (SFileLoader  *loader,
				    const char   *text,
				    int           len);
SFile *      sfile_loader_finish   (SFileLoader  *loader,
				    GError      **err);
void         sfile_loader_free     (SFileLoader  *loader);
#endif

/* - Writing - */

/* FIXME - not10: see if we can't get rid of the names. It
 * should be possible to pass NULL to state_transition_check()
 * and have it interprete that as "whatever". We would need
 * a way to get the name back then, though.
 */

SFileOutput *  sfile_output_new (SFormat       *format);
void     sfile_begin_add_record (SFileOutput       *file,
				 const char *name);
void     sfile_begin_add_list   (SFileOutput       *file,
				 const char *name);
void     sfile_end_add          (SFileOutput       *file,
				 const char *name,
				 gpointer object);
void     sfile_add_string       (SFileOutput       *file,
				 const char *name,
				 const char  *string);
void     sfile_add_integer      (SFileOutput       *file,
				 const char *name,
				 int          integer);
void     sfile_add_pointer      (SFileOutput       *file,
				 const char *name,
				 gpointer     pointer);
gboolean sfile_output_save      (SFileOutput       *sfile,
				 const char  *filename,
				 GError     **err);

void sfile_output_free (SFileOutput *sfile);
