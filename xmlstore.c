typedef struct ParsedItem ParsedItem;

typedef enum
{
    BEGIN,
    TEXT,
    END
} XmlItemType;

struct XmlItem
{
    XmlItemType type;
    char [1]	data;
};

struct XmlStore
{
    XmlItem *	items;

    GHashTable *user_data_map;
};

struct ParsedItem
{
    XmlItemType	type;

    union
    {
	struct
	{
	    char * element;
	    int	   n_attrs;
	    char **attr;
	} begin_item;

	struct
	{
	    char *text;
	} text_item;

	struct
	{
	    char *element;
	} end_item;
    } u;
};

static void
parse_begin_item (XmlItem    *item,
		  ParsedItem *parsed_item)
{
    
}

static void
parse_end_item (XmlItem    *item,
		ParsedItem *parsed_item)
{
    
}

static void
parse_text_item (XmlItem    *item,
		 ParsedItem *parsed_item)
{
    
}

static ParsedItem *
parsed_item_new (XmlItem *item)
{
    ParsedItem *parsed_item = g_new0 (ParsedItem, 0);
    
    switch (item->type)
    {
    case BEGIN:
	parsed_item->type = BEGIN;
	parse_begin_item (item, parsed_item);
	break;

    case END:
	parsed_item->type = END;
	parse_end_item (item, parsed_item);
	break;

    case TEXT:
	parsed_item->type = TEXT;
	parse_text_item (item, parsed_item);
	break;
    }

    return parsed_item;
}

static void
parsed_item_free (ParsedItem *item)
{
    
}
