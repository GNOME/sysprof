typedef struct SFormat SFormat;
typedef struct SFile SFile;

/* - Describing Types - */
SFormat *sformat_new (gpointer f);
gpointer sformat_new_record (const char  *name,
			     SFormat     *content,
			     ...);
gpointer sformat_new_list (const char  *name,
			   SFormat       *content);
gpointer sformat_new_pointer (const char  *name);
gpointer sformat_new_integer (const char  *name);
gpointer sformat_new_string (const char  *name);

/* - Reading - */
SFileInput *  sfile_load        (const char  *filename,
				 SFormat       *format,
				 GError     **err);
void     sfile_begin_get_record (SFileInput  *file);
int      sfile_begin_get_list   (SFileInput  *file);
void     sfile_get_pointer      (SFileInput  *file,
				 gpointer    *pointer);
void     sfile_get_integer      (SFileInput  *file,
				 int         *integer);
void     sfile_get_string       (SFileInput  *file,
				 char       **string);
void     sfile_end_get          (SFileInput  *file,
				 gpointer     object);

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
SFileOutput *  sfile_output_mew (SFormat       *format);
void     sfile_begin_add_record (SFile       *file,
				 gpointer     id);
void     sfile_begin_add_list   (SFile       *file,
				 gpointer     id);
void     sfile_end_add          (SFile       *file);
void     sfile_add_string       (SFile       *file,
				 const char  *string);
void     sfile_add_integer      (SFile       *file,
				 int          integer);
void     sfile_add_pointer      (SFile       *file,
				 gpointer     pointer);
gboolean sfile_save             (SFile       *sfile,
				 const char  *filename,
				 GError     **err);


