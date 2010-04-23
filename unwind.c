#include "elfparser.h"
#include <string.h>
#include "util.h"

/* Pointer encodings, from dwarf2.h.  */
typedef enum
{
    DW_EH_PE_absptr	= 0x00, /* */
    DW_EH_PE_omit	= 0xff, /* Value is not there */

    DW_EH_PE_uleb128	= 0x01,
    DW_EH_PE_udata2	= 0x02,
    DW_EH_PE_udata4	= 0x03,
    DW_EH_PE_udata8	= 0x04,
    DW_EH_PE_sleb128	= 0x09,
    DW_EH_PE_sdata2	= 0x0A,
    DW_EH_PE_sdata4	= 0x0B,
    DW_EH_PE_sdata8	= 0x0C,
    DW_EH_PE_signed	= 0x08,

    DW_EH_PE_pcrel	= 0x10, /* Value is *(cur + val) */
    DW_EH_PE_textrel	= 0x20, /* Value is *(&text + val) */
    DW_EH_PE_datarel	= 0x30, /* Value is *(&data + val) */
    DW_EH_PE_funcrel	= 0x40,	/* Value is *(fde.pc_begin + val) */
    DW_EH_PE_aligned	= 0x50, /* Value is absolute, and stored
				 * at next align */

    DW_EH_PE_indirect	= 0x80
} PointerEncoding;

typedef struct EncodedPointer EncodedPointer;
struct EncodedPointer
{
    PointerEncoding	encoding;
    guint64		value;
};

static guint64
get_length (const guchar **data)
{
    guint64 len;

    len = *(guint32 *)*data;

    *data += 4;
    
    if (len == 0xffffffff)
    {
	len = *(guint64 *)data;
	*data += 8;
    }

    return len;
}

static guint64
decode_uleb128 (const guchar **data)
{
    guint64 result;
    int shift;
    guchar b;

    result = 0;
    shift = 0;
    do
    {
	b = *(*data)++;
	result |= (b & 0x7f) << shift;
	shift += 7;

    } while (b & 0x80);

    return result;
}

static gint64
decode_sleb128 (const guchar **data)
{
    gint64 result;
    int shift;
    guchar b;

    result = 0;
    shift = 0;
    do
    {
	b = *(*data)++;
	result |= (b & 0x7f) << shift;
	shift += 7;
    } while (b & 0x80);

    if (b & 0x40 && shift < 64)
	result |= - (1 << shift);

    return result;
}

static void
decode_block (const guchar **data)
{
    int len;
    
    /* FIXME */

    len = decode_uleb128 (data);

    (*data) += len;
}

static gulong
decode_address (const guchar **data)
{
    /* FIXME */
    gulong r;

    r = *(guint32 *)*data;
    (*data) += 4;
    return r;
}

static const char *
decode_instruction (const guchar **data)
{
    int opcode = *(*data)++;
    int high2 = (opcode & 0xc0) >> 6;
    int low6 = (opcode & 0x3f);

    if (high2 == 0x01)
    {
	return "DW_CFA_advance_loc";
    }
    else if (high2 == 0x02)
    {
	g_print ("register: %d\n", low6);
	g_print ("offset: "FMT64"\n", decode_uleb128 (data));
	
	return "DW_CFA_offset";
    }
    else if (high2 == 0x03)
    {
	return "DW_CFA_restore";
    }
    else
    {
	g_assert ((opcode & 0xc0) == 0);
	
	switch (opcode)
	{
	case 0x0:
	    return "DW_CFA_nop";
	    
	case 0x01:
	    g_print ("addr: %p\n", (void *)decode_address (data));
	    return "DW_CFA_set_loc";

	case 0x02:
	    (*data)++;
	    return "DW_CFA_advance_loc1";

	case 0x03:
	    (*data) += 2;
	    return "DW_CFA_advance_loc2";

	case 0x04:
	    (*data) += 4;
	    return "DW_CFA_advance_loc4";

	case 0x05:
	    decode_uleb128 (data);
	    decode_uleb128 (data);
	    return "DW_CFA_offset_extended";

	case 0x06:
	    decode_uleb128 (data);
	    return "DW_CFA_restore_extended";

	case 0x07:
	    decode_uleb128 (data);
	    return "DW_CFA_undefined";

	case 0x08:
	    decode_uleb128 (data);
	    return "DW_CFA_same_value";

	case 0x09:
	    decode_uleb128 (data);
	    decode_uleb128 (data);
	    return "DW_CFA_register";

	case 0x0a:
	    return "DW_CFA_remember_state";

	case 0x0b:
	    return "DW_CFA_restore_state";

	case 0x0c:
	    g_print ("reg: "FMT64"\n", decode_uleb128 (data));
	    g_print ("off: "FMT64"\n", decode_uleb128 (data));
	    return "DW_CFA_def_cfa";

	case 0x0d:
	    decode_uleb128 (data);
	    return "DW_CFA_def_cfa_register";

	case 0x0e:
	    decode_uleb128 (data);
	    return "DW_CFA_def_cfa_offset";

	case 0x0f:
	    decode_block (data);
	    return "DW_CFA_def_cfa_expression";

	case 0x10:
	    decode_uleb128 (data);
	    decode_block (data);
	    return "DW_CFA_expression";

	case 0x11:
	    decode_uleb128 (data);
	    decode_sleb128 (data);
	    return "DW_CFA_offset_extended_sf";

	case 0x12:
	    decode_uleb128 (data);
	    decode_sleb128 (data);
	    return "DW_CFA_def_cfa_sf";

	case 0x13:
	    decode_sleb128 (data);
	    return "DW_CFA_def_cfa_offset_sf";

	case 0x14:
	    decode_uleb128 (data);
	    decode_uleb128 (data);
	    return "DW_CFA_val_offset";

	case 0x15:
	    decode_uleb128 (data);
	    decode_sleb128 (data);
	    return "DW_CFA_val_offset_sf";

	case 0x16:
	    decode_uleb128 (data);
	    decode_block (data);
	    return "DW_CFA_val_expression";

	case 0x1c:
	    return "DW_CFA_lo_user";

	case 0x3f:
	    return "DW_CFA_hi_user";

	default:
	    return "UNKNOWN INSTRUCTION";
	}
    }
}

typedef struct CIE CIE;
struct CIE
{
    PointerEncoding	encoding;
};

static void
decode_cie (const guchar **data, const guchar *end)
{
    gboolean has_augmentation;
    guint64 aug_len;
    const char *augmentation;
    CIE *cie;
    int i, field;
    
    g_print ("version: %d\n", *(*data)++);

    augmentation = (*data);
    
    *data += strlen (*data) + 1;

    g_print ("code alignment: "FMT64"\n", decode_uleb128 (data));

    g_print ("data alignment: %lld\n", decode_sleb128 (data));

    g_print ("return register: "FMT64"\n", decode_uleb128 (data));

    g_print ("augmentation: %s\n", augmentation);
    
    if (augmentation[0] == 'z')
    {
	aug_len = decode_uleb128 (data);

	g_print ("len: "FMT64"\n", aug_len);
	
	for (i = 1; augmentation[i] != 0; ++i)
	{
	    if (augmentation[i] == 'L')
	    {
		
	    }
	}
    }
    
    
    if (has_augmentation)
    {
	g_print ("%x\n", **data);
	
	*data += aug_len;
    }
    
    while (*data < end)
	g_print ("  %s\n", decode_instruction (data));
}

static gboolean
decode_fde (const guchar **data, const guchar *end)
{
    
    
    return FALSE;
}

static gboolean
decode_entry (const guchar **data, gboolean eh_frame)
{
    guint64 len;
    const guchar *end;
    gboolean is_cie;
    guint64 id;

    len = get_length (data);

    if (len == 0)
	return FALSE;
    
    end = *data + len;
    
    g_print ("length: "FMT64"\n", len);

    /* CIE_id is 0 for eh frames, and 0xffffffff/0xffffffffffffffff for .debug_frame */
    
    id = *(guint32 *)*data;

    g_print ("id: %lld\n", id);

    is_cie = (eh_frame && id == 0) || (!eh_frame && id == 0xffffffff);

    if (is_cie)
	g_print ("is cie\n");
    else
	g_print ("is not cie\n");
    
    *data += 4;

    if (is_cie)
	decode_cie (data, end);
    else
	decode_fde (data, end);
    
    return TRUE;
}

/* The correct API is probably something like
 *
 *   gboolean
 *   unwind (ElfParser   *parser,
 *           gulong      *regs
 *	     int	  n_regs,
 *           MemoryReader reader,
 *           gpointer     data);
 *
 */
void
unwind (ElfParser *elf)
{
    const guchar *data;
    gboolean eh_f;
    
    if ((data = elf_parser_get_debug_frame (elf)))
    {
	g_print ("Using .debug_frame\n");
	eh_f = FALSE;
    }
    else if ((data = elf_parser_get_eh_frame (elf)))
    {
	g_print ("Using .eh_frame\n");
	eh_f = TRUE;
    }
    else
    {
	g_print ("no debug info found\n");
	return;
    }

    while (decode_entry (&data, eh_f))
	return ;
	;
}

