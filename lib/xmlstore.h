typedef struct XmlIter XmlIter;
typedef struct XmlStore XmlStore;

XmlStore *xml_store_new           (void);
void      xml_store_free          (XmlStore   *store);
void      xml_store_append_begin  (XmlStore   *store,
				   const char *element);
void      xml_store_append_end    (XmlStore   *store,
				   const char *element);
void      xml_store_append_text   (XmlStore   *store,
				   const char *text);
void      xml_store_write         (XmlStore   *store,
				   const char *file,
				   GError     *file);
void      xml_store_set_data      (XmlIter    *iter,
				   gpointer    data);
gpointer  xml_store_get_data      (XmlIter    *iter,
				   gpointer    data);
void      xml_store_has_data      (XmlIter    *iter);

/* An iter stays valid as long as the XmlStore is valid */
XmlIter * xml_store_get_iter      (XmlStore   *store);
XmlIter * xml_iter_get_sibling    (XmlIter    *iter);
XmlIter * xml_iter_get_child      (XmlIter    *iter);
int       xml_iter_get_n_children (XmlIter    *iter);
gboolean  xml_iter_valid          (XmlIter    *iter);


void
process_tree (XmlIter *iter)
{
    XmlIter *i;
    
    if (!xml_iter_valid (iter))
	return;

    /* siblings */
    i = xml_iter_sibling (iter);
    while (xml_iter_valid (i))
    {
	process_tree (i);

	i = xml_iter_sibling (i);
    }

    /* children */
    process_tree (xml_iter_child (iter));
	
    
    process_tree (xml_iter_sibling (iter));
    process_tree (xml_iter_child (iter));
}

void
process_store (XmlStore *store)
{
    process_tree (xml_store_get_iter (store));
}
