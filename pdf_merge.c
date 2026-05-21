/*
 * pdf_merge.c
 * Basic PDF merger/splitter for standard PDFs
 * (unencrypted, non-linearised, standard xref tables — covers ~95% of business PDFs)
 *
 * Compile (MinGW):
 *   gcc -c pdf_merge.c -O2 -o pdf_merge.o
 */

#include "pdf_merge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================
 * TYPES
 * ============================================================ */

typedef struct {
    long long offset;
    int gen;
    int free;
} XRefEntry;

/* Object extracted from a compressed Object Stream (ObjStm, PDF 1.5+) */
typedef struct {
    int            obj_num;
    unsigned char *buf;     /* heap-allocated object content (no "N G obj"/"endobj" wrappers) */
    int            len;
} ExtractedObj;

typedef struct {
    unsigned char *data;
    long long       size;
    XRefEntry      *xref;          /* xref[obj_num] — indexed by obj number */
    int             xref_size;     /* length of xref array                  */
    int            *page_nums;     /* object numbers of pages in order      */
    int             page_count;
    int             catalog_num;
    unsigned char  *is_pages;      /* is_pages[i] = 1 if obj i is /Type /Pages */
    ExtractedObj   *extracted;     /* objects pulled from ObjStm streams    */
    int             extracted_count;
} PDF;

/* ============================================================
 * LOW-LEVEL HELPERS
 * ============================================================ */

static const unsigned char *
my_memmem(const unsigned char *hay, size_t hlen,
          const char *needle, size_t nlen)
{
    if (nlen == 0) return hay;
    if (hlen < nlen) return NULL;
    for (size_t i = 0; i <= hlen - nlen; i++)
        if (memcmp(hay + i, needle, nlen) == 0) return hay + i;
    return NULL;
}

/* Find last occurrence of needle in hay[0..hlen-1] */
static long long
find_last(const unsigned char *hay, long long hlen, const char *needle)
{
    int nlen = (int)strlen(needle);
    for (long long i = hlen - nlen; i >= 0; i--)
        if (memcmp(hay + i, needle, nlen) == 0) return i;
    return -1;
}

/* Skip whitespace (space, tab, CR, LF) at buf[*pos] */
static void skip_ws(const unsigned char *buf, long long size, long long *pos)
{
    while (*pos < size && (buf[*pos] == ' ' || buf[*pos] == '\t' ||
                           buf[*pos] == '\r' || buf[*pos] == '\n'))
        (*pos)++;
}

/* Read ASCII decimal integer at buf[*pos], advance *pos past it */
static long long
read_int(const unsigned char *buf, long long size, long long *pos)
{
    skip_ws(buf, size, pos);
    long long val = 0;
    while (*pos < size && isdigit(buf[*pos]))
        val = val * 10 + (buf[(*pos)++] - '0');
    return val;
}

/* Load file into heap-allocated buffer. Caller frees. */
static unsigned char *
load_file(const char *path, long long *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = 0;
    fclose(f);
    *out_size = sz;
    return buf;
}

/* ============================================================
 * PDF PARSING
 * ============================================================ */

/* Find startxref value from near end of file */
static long long
get_startxref(const unsigned char *data, long long size)
{
    long long search = size - 1024;
    if (search < 0) search = 0;
    long long pos = find_last(data + search, size - search, "startxref");
    if (pos < 0) return -1;
    pos += search + 9;
    skip_ws(data, size, &pos);
    return read_int(data, size, &pos);
}

/* Grow xref array so it can hold index `need-1`. Returns 1 on success. */
static int
xref_ensure(PDF *pdf, int need)
{
    if (need <= pdf->xref_size) return 1;
    int newsize = need + 256;
    XRefEntry *tmp = (XRefEntry *)realloc(pdf->xref, newsize * sizeof(XRefEntry));
    if (!tmp) return 0;
    memset(tmp + pdf->xref_size, 0, (newsize - pdf->xref_size) * sizeof(XRefEntry));
    pdf->xref = tmp;
    pdf->xref_size = newsize;
    return 1;
}

/* Parse standard xref table starting at pdf->data[xpos].
   Returns 1 if a valid "xref" table was found and parsed, 0 otherwise. */
static int
try_parse_standard_xref(PDF *pdf, long long xpos)
{
    if (xpos < 0 || xpos + 4 > pdf->size) return 0;
    if (memcmp(pdf->data + xpos, "xref", 4) != 0) return 0;

    long long pos = xpos + 4;

    while (pos < pdf->size) {
        skip_ws(pdf->data, pdf->size, &pos);
        if (pos + 7 <= pdf->size && memcmp(pdf->data + pos, "trailer", 7) == 0)
            break;
        if (pos >= pdf->size || !isdigit(pdf->data[pos])) break;

        int first = (int)read_int(pdf->data, pdf->size, &pos);
        int count = (int)read_int(pdf->data, pdf->size, &pos);
        /* Skip the newline that follows the subsection header */
        if (pos < pdf->size && pdf->data[pos] == '\r') pos++;
        if (pos < pdf->size && pdf->data[pos] == '\n') pos++;

        if (!xref_ensure(pdf, first + count)) return 0;

        for (int i = 0; i < count; i++) {
            if (pos + 17 > pdf->size) break;

            /* Parse 10-digit offset — read flexibly, not fixed-width */
            long long off = 0;
            int k;
            for (k = 0; k < 10 && pos < pdf->size && isdigit(pdf->data[pos]); k++)
                off = off * 10 + (pdf->data[pos++] - '0');
            if (pos < pdf->size && pdf->data[pos] == ' ') pos++;

            /* Parse 5-digit generation */
            int gen = 0;
            for (k = 0; k < 5 && pos < pdf->size && isdigit(pdf->data[pos]); k++)
                gen = gen * 10 + (pdf->data[pos++] - '0');
            if (pos < pdf->size && pdf->data[pos] == ' ') pos++;

            /* 'n' or 'f' */
            int is_free = 0;
            if (pos < pdf->size) { is_free = (pdf->data[pos] == 'f') ? 1 : 0; pos++; }

            /* EOL: spec says 2 bytes (SP+CR, SP+LF, or CR+LF).
               Some non-compliant generators write only \n — handle both. */
            if (pos < pdf->size && (pdf->data[pos] == ' ' || pdf->data[pos] == '\r')) pos++;
            if (pos < pdf->size && pdf->data[pos] == '\n') pos++;

            /* First-wins: caller traverses newest→oldest so skip if already populated */
            if (pdf->xref[first + i].offset == 0 && !pdf->xref[first + i].free) {
                pdf->xref[first + i].offset = off;
                pdf->xref[first + i].gen    = gen;
                pdf->xref[first + i].free   = is_free;
            }
        }
    }
    return 1;
}

/* Fallback: scan entire file for "N G obj" lines and record their offsets.
   Handles PDF 1.5+ cross-reference streams, linearized PDFs, and anything
   else that doesn't have a plain-text xref table. */
static void
build_xref_by_scan(PDF *pdf)
{
    for (long long i = 0; i < pdf->size - 8; i++) {
        if (!isdigit(pdf->data[i])) continue;
        /* Must be at the start of a line */
        if (i > 0 && pdf->data[i-1] != '\n' && pdf->data[i-1] != '\r') continue;

        long long pp = i;
        int obj_num = 0;
        while (pp < pdf->size && isdigit(pdf->data[pp]))
            obj_num = obj_num * 10 + (pdf->data[pp++] - '0');
        if (pp >= pdf->size || pdf->data[pp] != ' ') continue;
        pp++;
        if (!isdigit(pdf->data[pp])) continue;
        int gen = 0;
        while (pp < pdf->size && isdigit(pdf->data[pp]))
            gen = gen * 10 + (pdf->data[pp++] - '0');
        if (pp >= pdf->size || pdf->data[pp] != ' ') continue;
        pp++;
        if (pp + 3 > pdf->size || memcmp(pdf->data + pp, "obj", 3) != 0) continue;

        /* Valid "N G obj" found */
        if (obj_num <= 0 || obj_num > 1000000) continue;
        if (!xref_ensure(pdf, obj_num + 1)) continue;
        /* Only record if slot is empty and not already marked free */
        if (pdf->xref[obj_num].offset == 0 && !pdf->xref[obj_num].free) {
            pdf->xref[obj_num].offset = i;
            pdf->xref[obj_num].gen    = gen;
            pdf->xref[obj_num].free   = 0;
        }
    }
}

/* Find the Catalog object number.
 *
 * Strategy A: scan the entire file for "/Root" followed by "N 0 R".
 *   This works for both classic PDFs (trailer dict) and PDF 1.5+ PDFs
 *   (XRef stream dict — no "trailer" keyword at all).  We keep the LAST
 *   valid hit so that incremental-update PDFs use the newest root.
 *
 * Strategy B (fallback): scan for an object whose dictionary contains
 *   "/Type /Catalog" or "/Type/Catalog", then walk back to its obj header.
 *   Walk-back window is 4096 bytes to handle large catalog dictionaries.
 */
static void
find_catalog_num(PDF *pdf)
{
    /* --- Strategy A: search for /Root anywhere in the file --- */
    long long i = 0;
    while (i + 5 < pdf->size) {
        const unsigned char *p = my_memmem(pdf->data + i,
                                           (size_t)(pdf->size - i), "/Root", 5);
        if (!p) break;
        long long rpos = (p - pdf->data) + 5;
        skip_ws(pdf->data, pdf->size, &rpos);
        if (rpos < pdf->size && isdigit(pdf->data[rpos])) {
            long long rpos2 = rpos;
            int rnum = (int)read_int(pdf->data, pdf->size, &rpos2);
            /* Confirm it looks like "N 0 R" (gen is almost always 0) */
            skip_ws(pdf->data, pdf->size, &rpos2);
            long long rpos3 = rpos2;
            read_int(pdf->data, pdf->size, &rpos3); /* skip gen */
            skip_ws(pdf->data, pdf->size, &rpos3);
            if (rnum > 0 &&
                rpos3 < pdf->size &&
                (pdf->data[rpos3] == 'R' || pdf->data[rpos3] == 'r'))
                pdf->catalog_num = rnum;
        }
        i = (p - pdf->data) + 5;
    }
    if (pdf->catalog_num > 0) return;

    /* --- Strategy B: locate /Type /Catalog (or /Type/Catalog) --- */
    const char *patterns[] = { "/Type /Catalog", "/Type/Catalog", NULL };
    for (int pi = 0; patterns[pi] && pdf->catalog_num <= 0; pi++) {
        int plen = (int)strlen(patterns[pi]);
        long long search = 0;
        while (search + plen <= pdf->size) {
            const unsigned char *c = my_memmem(pdf->data + search,
                                               (size_t)(pdf->size - search),
                                               patterns[pi], plen);
            if (!c) break;
            /* Walk back up to 4096 bytes to find "N G obj" header */
            long long hit = c - pdf->data;
            long long limit = hit > 4096 ? hit - 4096 : 0;
            for (long long j = hit; j >= limit; j--) {
                if (j > 0 &&
                    pdf->data[j-1] != '\n' && pdf->data[j-1] != '\r') continue;
                if (!isdigit(pdf->data[j])) continue;
                long long pp = j;
                int n = 0;
                while (pp < pdf->size && isdigit(pdf->data[pp]))
                    n = n * 10 + (pdf->data[pp++] - '0');
                if (pp < pdf->size && pdf->data[pp] == ' ' && n > 0) {
                    pdf->catalog_num = n;
                    return;
                }
            }
            search = hit + plen;
        }
    }
}

/* ============================================================
 * XREF STREAM PARSER  (PDF 1.5+ — required for digitally signed PDFs)
 *
 * Signed PDFs (and most modern PDFs) use compressed cross-reference streams
 * instead of, or in addition to, plain "xref" tables.  Each update appended
 * during signing adds a new xref stream whose /Prev key points backward to
 * the previous one.  We must walk this chain newest→oldest so that the most
 * recent version of every object (the signed/updated one) wins.
 *
 * dict_get_int() and flate_decompress() are defined later in this file;
 * forward-declare them so the xref-stream parser can call them.
 * ============================================================ */

static int           dict_get_int    (const unsigned char *, int, const char *); /* fwd */
static unsigned char *flate_decompress(const unsigned char *, int, int *);        /* fwd */

/* Read a big-endian integer of `w` bytes from data[*pos]. Advances *pos by w. */
static long long
xref_read_field(const unsigned char *data, int w, long long *pos)
{
    long long val = 0;
    for (int b = 0; b < w; b++) val = (val << 8) | data[(*pos)++];
    return val;
}

/* Extract /Prev integer from a dict slice. Returns -1 if not found. */
static long long
dict_get_prev(const unsigned char *data, int len)
{
    const unsigned char *p = my_memmem(data, (size_t)len, "/Prev", 5);
    if (!p) return -1;
    long long pos = (p - data) + 5;
    skip_ws(data, len, &pos);
    if (pos >= len || !isdigit(data[pos])) return -1;
    return read_int(data, len, &pos);
}

/*
 * Parse one XRef Stream object at file offset `xpos` (PDF 1.5+).
 * Fills pdf->xref with first-wins semantics (caller must walk newest→oldest).
 * Returns /Prev offset to continue the chain, or -1 if chain ends.
 *
 * Type-0 entries → mark as free.
 * Type-1 entries → direct byte-offset (normal object).
 * Type-2 entries → object lives inside an ObjStm; set offset sentinel -1 so
 *                  the slot is "claimed" and build_xref_by_scan won't overwrite
 *                  it.  extract_all_objstm() will populate pdf->extracted later.
 */
static long long
parse_one_xref_stream(PDF *pdf, long long xpos)
{
    if (xpos < 0 || xpos + 10 > pdf->size) return -1;

    /* Locate the "obj" keyword within a short window from xpos */
    long long head_avail = pdf->size - xpos;
    if (head_avail > 256) head_avail = 256;
    const unsigned char *obj_kw =
        my_memmem(pdf->data + xpos, (size_t)head_avail, " obj", 4);
    if (!obj_kw) return -1;

    /* Dict starts right after "obj" + optional whitespace */
    const unsigned char *dict_start = obj_kw + 4;
    while (dict_start < pdf->data + pdf->size &&
           (*dict_start == '\r' || *dict_start == '\n' || *dict_start == ' '))
        dict_start++;
    if (dict_start >= pdf->data + pdf->size || *dict_start != '<') return -1;

    /* Find "stream" keyword to delimit the dictionary */
    long long dict_avail = pdf->size - (dict_start - pdf->data);
    if (dict_avail > 32768) dict_avail = 32768;
    const unsigned char *stream_kw =
        my_memmem(dict_start, (size_t)dict_avail, "stream", 6);
    if (!stream_kw) return -1;
    int dict_len = (int)(stream_kw - dict_start);

    /* /W array — three integers [w0 w1 w2]; w0 default 1, w1 must be >0, w2 default 0 */
    int w0 = 1, w1 = 4, w2 = 0;
    const unsigned char *wp = my_memmem(dict_start, (size_t)dict_len, "/W", 2);
    if (wp) {
        long long wpos = (wp - dict_start) + 2;
        while (wpos < dict_len && dict_start[wpos] != '[') wpos++;
        if (wpos < dict_len && dict_start[wpos] == '[') {
            wpos++;
            w0 = (int)read_int(dict_start, dict_len, &wpos);
            w1 = (int)read_int(dict_start, dict_len, &wpos);
            w2 = (int)read_int(dict_start, dict_len, &wpos);
        }
    }
    if (w1 <= 0 || w1 > 8) return -1;

    /* /Size */
    int xref_sz = dict_get_int(dict_start, dict_len, "Size");
    if (xref_sz <= 0 || !xref_ensure(pdf, xref_sz)) return -1;

    /* /Index pairs [first count ...]; default [0 Size] */
    int index_pairs[512];
    int n_pairs = 0;
    const unsigned char *idxp = my_memmem(dict_start, (size_t)dict_len, "/Index", 6);
    if (idxp) {
        long long ipos = (idxp - dict_start) + 6;
        while (ipos < dict_len && dict_start[ipos] != '[') ipos++;
        if (ipos < dict_len && dict_start[ipos] == '[') {
            ipos++;
            while (ipos < dict_len && dict_start[ipos] != ']' && n_pairs < 510) {
                skip_ws(dict_start, dict_len, &ipos);
                if (dict_start[ipos] == ']') break;
                if (!isdigit(dict_start[ipos])) { ipos++; continue; }
                index_pairs[n_pairs++] = (int)read_int(dict_start, dict_len, &ipos);
            }
        }
    }
    if (n_pairs == 0 || (n_pairs & 1)) {
        index_pairs[0] = 0; index_pairs[1] = xref_sz; n_pairs = 2;
    }

    /* Stream body */
    int stream_length = dict_get_int(dict_start, dict_len, "Length");
    const unsigned char *stream_body = stream_kw + 6;
    if (stream_body < pdf->data + pdf->size && *stream_body == '\r') stream_body++;
    if (stream_body < pdf->data + pdf->size && *stream_body == '\n') stream_body++;
    long long body_avail = pdf->size - (stream_body - pdf->data);
    if (stream_length <= 0 || stream_length > (int)body_avail)
        stream_length = (int)body_avail;

    /* Decompress if FlateDecode */
    int is_flate =
        (my_memmem(dict_start, (size_t)dict_len, "FlateDecode", 11) != NULL ||
         my_memmem(dict_start, (size_t)dict_len, "/Fl\n",       4)  != NULL ||
         my_memmem(dict_start, (size_t)dict_len, "/Fl ",        4)  != NULL);

    const unsigned char *xref_data     = stream_body;
    int                  xref_data_len = stream_length;
    unsigned char       *decomp_buf    = NULL;

    if (is_flate) {
        int dl;
        decomp_buf = flate_decompress(stream_body, stream_length, &dl);
        if (!decomp_buf || dl < 0) { free(decomp_buf); return -1; }
        xref_data     = decomp_buf;
        xref_data_len = dl;
    }

    /* Parse binary entries */
    int       entry_size = w0 + w1 + w2;
    long long dpos       = 0;
    if (entry_size <= 0) { free(decomp_buf); return -1; }

    for (int pi = 0; pi + 1 < n_pairs; pi += 2) {
        int seg_first = index_pairs[pi];
        int seg_count = index_pairs[pi + 1];
        if (!xref_ensure(pdf, seg_first + seg_count)) break;
        for (int ii = 0; ii < seg_count; ii++) {
            if (dpos + entry_size > xref_data_len) goto xrs_done;

            long long f1 = (w0 > 0) ? xref_read_field(xref_data, w0, &dpos) : 1;
            long long f2 =             xref_read_field(xref_data, w1, &dpos);
            long long f3 = (w2 > 0) ? xref_read_field(xref_data, w2, &dpos) : 0;

            int obj_idx = seg_first + ii;
            /* First-wins: skip if slot already occupied */
            if (pdf->xref[obj_idx].offset != 0 || pdf->xref[obj_idx].free) continue;

            if (f1 == 0) {
                /* Type 0: free object */
                pdf->xref[obj_idx].free = 1;
            } else if (f1 == 1) {
                /* Type 1: uncompressed at byte offset f2, gen f3 */
                pdf->xref[obj_idx].offset = (long long)f2;
                pdf->xref[obj_idx].gen    = (int)f3;
            } else if (f1 == 2) {
                /* Type 2: compressed in ObjStm f2, index f3.
                   Set sentinel offset -1 so build_xref_by_scan skips this slot.
                   extract_all_objstm() will populate pdf->extracted instead. */
                pdf->xref[obj_idx].offset = -1;
                pdf->xref[obj_idx].gen    = 0;
            }
        }
    }
xrs_done:;

    long long prev = dict_get_prev(dict_start, dict_len);
    free(decomp_buf);
    return prev;
}

/* Extract /Prev from the trailer dict that follows an "xref" table at `xpos`.
   Scans forward from xpos for the "trailer" keyword. Returns -1 if not found. */
static long long
parse_trailer_prev(PDF *pdf, long long xpos)
{
    if (xpos < 0) return -1;
    long long limit = xpos + 262144;          /* shouldn't be further than 256 KB away */
    if (limit > pdf->size - 7) limit = pdf->size - 7;
    const unsigned char *p =
        my_memmem(pdf->data + xpos, (size_t)(limit - xpos + 7), "trailer", 7);
    if (!p) return -1;
    long long tpos = (p - pdf->data) + 7;
    skip_ws(pdf->data, pdf->size, &tpos);
    if (tpos + 1 >= pdf->size) return -1;
    /* Walk to end of trailer dict */
    int depth = 0;
    long long dp = tpos;
    while (dp + 1 < pdf->size) {
        if (pdf->data[dp] == '<' && pdf->data[dp + 1] == '<') { depth++; dp += 2; }
        else if (pdf->data[dp] == '>' && pdf->data[dp + 1] == '>') {
            if (--depth == 0) { dp += 2; break; }
            dp += 2;
        } else dp++;
    }
    return dict_get_prev(pdf->data + tpos, (int)(dp - tpos));
}

/* Parse the xref table (or fall back to full-file scan) and find /Root.
   Fills pdf->xref, pdf->xref_size, pdf->catalog_num.
   Returns 0 on success. */
static int
parse_xref(PDF *pdf)
{
    pdf->xref_size = 256;
    pdf->xref = (XRefEntry *)calloc(pdf->xref_size, sizeof(XRefEntry));
    if (!pdf->xref) return -1;

    long long xpos = get_startxref(pdf->data, pdf->size);

    if (xpos >= 0) {
        /* Walk the entire xref chain newest → oldest.
         * Each iteration parses one xref section (table or stream) and
         * follows /Prev to the previous one.  Because we process newest
         * first and both parsers use first-wins semantics, the most recent
         * version of every object (the one after signing / incremental
         * updates) is always the one that lands in pdf->xref. */
        long long visited[64];
        int n_visited = 0;
        long long cur = xpos;

        while (cur >= 0 && n_visited < 64) {
            /* Cycle guard */
            int loop = 0;
            for (int vi = 0; vi < n_visited; vi++)
                if (visited[vi] == cur) { loop = 1; break; }
            if (loop) break;
            visited[n_visited++] = cur;

            long long prev;
            if (cur + 4 <= pdf->size &&
                memcmp(pdf->data + cur, "xref", 4) == 0) {
                /* Standard plain-text xref table */
                try_parse_standard_xref(pdf, cur);
                prev = parse_trailer_prev(pdf, cur);
            } else {
                /* PDF 1.5+ compressed xref stream (also used by signed PDFs) */
                prev = parse_one_xref_stream(pdf, cur);
            }
            cur = prev;
        }
    }

    /* Supplement with a full-file object scan.
     * This catches objects that are genuinely missing from the xref chain
     * (malformed PDFs, partially-written files) without overwriting entries
     * already populated by the chain walk above. */
    build_xref_by_scan(pdf);

    find_catalog_num(pdf);

    return (pdf->catalog_num > 0) ? 0 : -1;
}

/* Return pointer to start of object N's content (between "obj" and "endobj"),
   and set *out_len. Does NOT include the "N G obj" header line.
   For objects extracted from ObjStm the returned pointer is into a heap buffer.
   Returns NULL if not found. */
static const unsigned char *
get_obj_content(PDF *pdf, int num, int *out_len)
{
    /* 1. Check objects extracted from compressed ObjStm streams first */
    for (int i = 0; i < pdf->extracted_count; i++) {
        if (pdf->extracted[i].obj_num == num) {
            *out_len = pdf->extracted[i].len;
            return pdf->extracted[i].buf;
        }
    }

    /* 2. Fall back to direct byte-offset lookup */
    if (num <= 0 || num >= pdf->xref_size) return NULL;
    if (pdf->xref[num].free) return NULL;
    long long off = pdf->xref[num].offset;
    if (off <= 0 || off >= pdf->size) return NULL;

    const unsigned char *start = pdf->data + off;
    long long avail = pdf->size - off;

    /* Skip "N G obj" header: find the newline after "obj" */
    const unsigned char *obj_kw = my_memmem(start, (size_t)(avail > 64 ? 64 : avail), " obj", 4);
    if (!obj_kw) return NULL;
    const unsigned char *content = obj_kw + 4;
    if (content < pdf->data + pdf->size && *content == '\r') content++;
    if (content < pdf->data + pdf->size && *content == '\n') content++;

    long long content_avail = pdf->size - (content - pdf->data);

    /* Find "endobj" — skip past "endstream" first if present */
    const unsigned char *endstream = my_memmem(content, (size_t)content_avail,
                                               "endstream", 9);
    const unsigned char *search_start = endstream ? endstream + 9 : content;
    long long search_avail = pdf->size - (search_start - pdf->data);
    const unsigned char *endobj = my_memmem(search_start, (size_t)search_avail,
                                            "endobj", 6);
    if (!endobj) return NULL;

    *out_len = (int)(endobj - content);
    return content;
}

/* Look for /Type /Name in an object and return the Name string (without '/').
   Returns 1 if found, 0 otherwise. */
static int
get_type_name(const unsigned char *content, int len, char *out, int out_max)
{
    const unsigned char *p = my_memmem(content, (size_t)len, "/Type", 5);
    if (!p) return 0;
    long long pos = (p - content) + 5;
    /* skip whitespace */
    while (pos < len && (content[pos] == ' ' || content[pos] == '\r' || content[pos] == '\n'))
        pos++;
    if (pos >= len || content[pos] != '/') return 0;
    pos++;
    int i = 0;
    while (pos < len && i < out_max - 1 && !isspace(content[pos]) &&
           content[pos] != '/' && content[pos] != '>' && content[pos] != ']')
        out[i++] = content[pos++];
    out[i] = 0;
    return i > 0;
}

/* Get a single reference value for /Key in content.
   Returns the object number (gen 0 assumed), or 0 if not found. */
static int
get_ref(const unsigned char *content, int len, const char *key)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "/%s", key);
    const unsigned char *p = my_memmem(content, (size_t)len, needle, strlen(needle));
    if (!p) return 0;
    long long pos = (p - content) + (int)strlen(needle);
    while (pos < len && (content[pos] == ' ' || content[pos] == '\t' ||
                         content[pos] == '\r' || content[pos] == '\n')) pos++;
    if (!isdigit(content[pos])) return 0;
    int num = 0;
    while (pos < len && isdigit(content[pos])) num = num * 10 + (content[pos++] - '0');
    /* skip gen */
    while (pos < len && (content[pos] == ' ' || content[pos] == '\t')) pos++;
    while (pos < len && isdigit(content[pos])) pos++;
    while (pos < len && (content[pos] == ' ' || content[pos] == '\t')) pos++;
    if (pos < len && content[pos] == 'R') return num;
    return 0;
}

/* Parse /Kids array from a Pages object. Returns count of kids. */
static int
parse_kids(const unsigned char *content, int len, int *kids, int max_kids)
{
    const unsigned char *p = my_memmem(content, (size_t)len, "/Kids", 5);
    if (!p) return 0;
    long long pos = (p - content) + 5;
    while (pos < len && content[pos] != '[') pos++;
    if (pos >= len) return 0;
    pos++;
    int count = 0;
    while (pos < len && content[pos] != ']' && count < max_kids) {
        while (pos < len && (content[pos] == ' ' || content[pos] == '\t' ||
                             content[pos] == '\r' || content[pos] == '\n')) pos++;
        if (content[pos] == ']') break;
        if (!isdigit(content[pos])) { pos++; continue; }
        int num = 0;
        while (pos < len && isdigit(content[pos])) num = num * 10 + (content[pos++] - '0');
        /* skip gen and R */
        while (pos < len && content[pos] != 'R' && content[pos] != ']') pos++;
        if (pos < len && content[pos] == 'R') { kids[count++] = num; pos++; }
    }
    return count;
}

/* Recursively collect Page object numbers in document order */
static void
collect_pages_recursive(PDF *pdf, int pages_obj_num, int *page_list, int *count, int max)
{
    int len;
    const unsigned char *c = get_obj_content(pdf, pages_obj_num, &len);
    if (!c) return;

    char type_name[32] = "";
    get_type_name(c, len, type_name, sizeof(type_name));

    if (strcmp(type_name, "Page") == 0) {
        if (*count < max) page_list[(*count)++] = pages_obj_num;
        return;
    }

    /* Must be /Pages — parse /Kids */
    int kids[4096];
    int nkids = parse_kids(c, len, kids, 4096);
    for (int i = 0; i < nkids; i++)
        collect_pages_recursive(pdf, kids[i], page_list, count, max);
}

/* ============================================================
 * OBJSTM EXTRACTION  (PDF 1.5+ compressed object streams)
 *
 * Modern PDFs (Office, Acrobat) store many lightweight objects — Catalog,
 * Pages, Page dicts — inside FlateDecode-compressed ObjStm objects.
 * Those objects have NO "N G obj" line in the raw file, so the xref scanner
 * misses them completely. We must decompress each ObjStm and register its
 * contents in pdf->extracted so get_obj_content() can find them.
 * ============================================================ */

#include "miniz.h"

/* Read a /Key integer from a PDF dictionary slice (no stream). Returns 0 if not found. */
static int
dict_get_int(const unsigned char *data, int len, const char *key)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "/%s", key);
    const unsigned char *p = my_memmem(data, (size_t)len, needle, strlen(needle));
    if (!p) return 0;
    long long pos = (p - data) + (int)strlen(needle);
    skip_ws(data, len, &pos);
    if (pos >= len || !isdigit(data[pos])) return 0;
    return (int)read_int(data, len, &pos);
}

/* Decompress a FlateDecode stream. Returns heap buffer; caller frees.
   out_len receives decompressed size, or -1 on error. */
static unsigned char *
flate_decompress(const unsigned char *src, int src_len, int *out_len)
{
    /* Start with 8× the compressed size; keep doubling on MZ_BUF_ERROR */
    mz_ulong dest_len = (mz_ulong)src_len * 8 + 1024;
    unsigned char *dest = NULL;
    for (int tries = 0; tries < 6; tries++) {
        free(dest);
        dest = (unsigned char *)malloc((size_t)dest_len + 1);
        if (!dest) { *out_len = -1; return NULL; }
        mz_ulong dl = dest_len;
        int r = mz_uncompress(dest, &dl, src, (mz_ulong)src_len);
        if (r == MZ_OK) {
            dest[dl] = 0;
            *out_len = (int)dl;
            return dest;
        }
        if (r != MZ_BUF_ERROR) break;
        dest_len *= 2;
    }
    free(dest);
    *out_len = -1;
    return NULL;
}

/* Extract all objects from a single ObjStm and append to pdf->extracted. */
static void
extract_one_objstm(PDF *pdf, int objstm_num)
{
    int clen;
    const unsigned char *c = get_obj_content(pdf, objstm_num, &clen);
    if (!c) return;

    /* Must be /Type /ObjStm — also accept missing /Type for robustness */
    int n_objs = dict_get_int(c, clen, "N");
    int first  = dict_get_int(c, clen, "First");
    if (n_objs <= 0 || first <= 0 || n_objs > 65536) return;

    /* Locate the stream body */
    const unsigned char *sp = (const unsigned char *)
        my_memmem(c, (size_t)clen, "stream", 6);
    if (!sp) return;
    const unsigned char *stream_data = sp + 6;
    if (stream_data < pdf->data + pdf->size && *stream_data == '\r') stream_data++;
    if (stream_data < pdf->data + pdf->size && *stream_data == '\n') stream_data++;

    /* Find endstream */
    long long stream_avail = clen - (int)(stream_data - c);
    const unsigned char *esp = (const unsigned char *)
        my_memmem(stream_data, (size_t)stream_avail, "endstream", 9);
    if (!esp) return;
    int compressed_len = (int)(esp - stream_data);
    if (compressed_len <= 0) return;

    /* Decompress (only FlateDecode is supported; skip others) */
    if (!my_memmem(c, (size_t)clen, "FlateDecode", 11) &&
        !my_memmem(c, (size_t)clen, "Fl\n",        3)  &&
        !my_memmem(c, (size_t)clen, "Fl ",         3))
        return;  /* unsupported filter */

    int decomp_len;
    unsigned char *decomp = flate_decompress(stream_data, compressed_len, &decomp_len);
    if (!decomp || decomp_len < first) { free(decomp); return; }

    /* Parse the header: n_objs pairs of (obj_num, byte_offset_within_content_area) */
    int *obj_nums = (int *)malloc(n_objs * sizeof(int));
    int *obj_offs = (int *)malloc(n_objs * sizeof(int));
    if (!obj_nums || !obj_offs) {
        free(decomp); free(obj_nums); free(obj_offs); return;
    }
    long long hp = 0;
    for (int i = 0; i < n_objs; i++) {
        obj_nums[i] = (int)read_int(decomp, decomp_len, &hp);
        obj_offs[i] = (int)read_int(decomp, decomp_len, &hp);
    }

    /* Grow extracted array once for all objects in this ObjStm */
    ExtractedObj *tmp = (ExtractedObj *)realloc(pdf->extracted,
        (pdf->extracted_count + n_objs) * sizeof(ExtractedObj));
    if (!tmp) { free(decomp); free(obj_nums); free(obj_offs); return; }
    pdf->extracted = tmp;

    for (int i = 0; i < n_objs; i++) {
        /* Don't add if we already have this object (ObjStm in incremental update) */
        int already = 0;
        for (int k = 0; k < pdf->extracted_count; k++) {
            if (pdf->extracted[k].obj_num == obj_nums[i]) { already = 1; break; }
        }
        if (already) continue;

        int start = first + obj_offs[i];
        int end   = (i < n_objs - 1) ? (first + obj_offs[i + 1]) : decomp_len;
        if (start < 0 || end > decomp_len || start >= end) continue;

        int olen = end - start;
        unsigned char *obuf = (unsigned char *)malloc((size_t)olen + 1);
        if (!obuf) continue;
        memcpy(obuf, decomp + start, olen);
        obuf[olen] = 0;

        pdf->extracted[pdf->extracted_count].obj_num = obj_nums[i];
        pdf->extracted[pdf->extracted_count].buf     = obuf;
        pdf->extracted[pdf->extracted_count].len     = olen;
        pdf->extracted_count++;
    }

    free(decomp);
    free(obj_nums);
    free(obj_offs);
}

/* Scan all directly-accessible objects for /Type /ObjStm and extract them. */
static void
extract_all_objstm(PDF *pdf)
{
    for (int i = 1; i < pdf->xref_size; i++) {
        if (pdf->xref[i].free || pdf->xref[i].offset <= 0) continue;
        int len;
        const unsigned char *c = get_obj_content(pdf, i, &len);
        if (!c) continue;
        /* Quick check: must contain /ObjStm to avoid scanning every object */
        if (!my_memmem(c, (size_t)len, "ObjStm", 6)) continue;
        char tname[32] = "";
        get_type_name(c, len, tname, sizeof(tname));
        if (strcmp(tname, "ObjStm") == 0)
            extract_one_objstm(pdf, i);
    }
}

/* ============================================================
 * BUILD PDF OBJECT MAPS
 * ============================================================ */

static int
pdf_parse_full(PDF *pdf)
{
    if (parse_xref(pdf) != 0) return -1;
    if (pdf->catalog_num <= 0) return -1;

    /* Decompress all ObjStm objects so their contents are accessible
       via get_obj_content() for the steps that follow. */
    extract_all_objstm(pdf);

    /* If catalog was found via Strategy A but the actual catalog object
       wasn't directly accessible (it's inside an ObjStm), re-verify it now. */
    if (pdf->catalog_num > 0) {
        int len;
        const unsigned char *c = get_obj_content(pdf, pdf->catalog_num, &len);
        if (!c) {
            /* Try to find it in extracted objects */
            pdf->catalog_num = 0;
            find_catalog_num(pdf); /* re-run now that extracted is populated */
        }
    }
    if (pdf->catalog_num <= 0) return -1;

    /* Allocate is_pages flags */
    pdf->is_pages = (unsigned char *)calloc(pdf->xref_size + pdf->extracted_count + 1, 1);
    if (!pdf->is_pages) return -1;

    /* Find root Pages object via Catalog */
    int len;
    const unsigned char *cat = get_obj_content(pdf, pdf->catalog_num, &len);
    if (!cat) return -1;
    int root_pages = get_ref(cat, len, "Pages");
    if (root_pages <= 0) return -1;

    /* Mark all /Type /Pages objects — check both direct and ObjStm objects */
    for (int i = 1; i < pdf->xref_size; i++) {
        if (pdf->xref[i].free || pdf->xref[i].offset <= 0) continue;
        const unsigned char *c = get_obj_content(pdf, i, &len);
        if (!c) continue;
        char tname[32] = "";
        if (get_type_name(c, len, tname, sizeof(tname)) &&
            strcmp(tname, "Pages") == 0)
            pdf->is_pages[i] = 1;
    }
    for (int i = 0; i < pdf->extracted_count; i++) {
        const unsigned char *c = pdf->extracted[i].buf;
        int clen = pdf->extracted[i].len;
        char tname[32] = "";
        if (get_type_name(c, clen, tname, sizeof(tname)) &&
            strcmp(tname, "Pages") == 0) {
            int n = pdf->extracted[i].obj_num;
            if (!xref_ensure(pdf, n + 1)) continue;
            pdf->is_pages[n] = 1;
        }
    }

    /* Collect pages */
    pdf->page_nums = (int *)malloc(65536 * sizeof(int));
    if (!pdf->page_nums) return -1;
    pdf->page_count = 0;
    collect_pages_recursive(pdf, root_pages, pdf->page_nums, &pdf->page_count, 65536);
    if (pdf->page_count == 0) return -1;  /* guard: fail loudly rather than write empty PDF */
    return 0;
}

static void
pdf_free(PDF *pdf)
{
    free(pdf->data);
    free(pdf->xref);
    free(pdf->is_pages);
    free(pdf->page_nums);
    for (int i = 0; i < pdf->extracted_count; i++)
        free(pdf->extracted[i].buf);
    free(pdf->extracted);
    memset(pdf, 0, sizeof(*pdf));
}

/* ============================================================
 * REFERENCE REWRITING
 * Write object content to out_file with obj_map applied to all
 * "N G R" references.  obj_map[N] == 0 means "no mapping — keep N".
 * Stream data (after the "stream" keyword) is always copied verbatim.
 * ============================================================ */

static void
write_rewritten_obj(FILE *out, int new_num,
                    const unsigned char *content, int content_len,
                    const int *obj_map, int map_size,
                    long long *offset_out)
{
    *offset_out = ftell(out);
    fprintf(out, "%d 0 obj\n", new_num);

    /* Find "stream" keyword preceded by \n, \r, or '>' to avoid false hits. */
    const unsigned char *stream_pos = NULL;
    {
        const unsigned char *p = content;
        long long rem = content_len;
        while (rem >= 6) {
            const unsigned char *hit = (const unsigned char *)
                my_memmem(p, (size_t)rem, "stream", 6);
            if (!hit) break;
            long long idx = hit - content;
            unsigned char prev = (idx > 0) ? content[idx - 1] : 0;
            if (prev == '\n' || prev == '\r' || prev == '>') {
                stream_pos = hit;
                break;
            }
            p   = hit + 1;
            rem = content_len - (int)(p - content);
        }
    }

    int dict_len = stream_pos ? (int)(stream_pos - content) : content_len;

    /* Rewrite dictionary portion, translating "N G R" references. */
    int i = 0;
    while (i < dict_len) {
        if (isdigit(content[i])) {
            int num_start = i;
            long long num = 0;
            while (i < dict_len && isdigit(content[i])) num = num * 10 + (content[i++] - '0');
            int after_num = i;
            int j = i;
            while (j < dict_len && (content[j] == ' ' || content[j] == '\t')) j++;
            int gen_start = j;
            while (j < dict_len && isdigit(content[j])) j++;
            int gen_end = j;
            while (j < dict_len && (content[j] == ' ' || content[j] == '\t')) j++;
            if (j < dict_len && content[j] == 'R' && gen_end > gen_start) {
                long long new_ref = num;
                if (num > 0 && num < (long long)map_size && obj_map[num] > 0)
                    new_ref = obj_map[num];
                fprintf(out, "%lld ", new_ref);
                fwrite(content + gen_start, 1, gen_end - gen_start, out);
                fputc(' ', out);
                fputc('R', out);
                i = j + 1;
            } else {
                fwrite(content + num_start, 1, after_num - num_start, out);
            }
        } else {
            fputc(content[i++], out);
        }
    }

    if (stream_pos)
        fwrite(stream_pos, 1, content_len - dict_len, out);

    fprintf(out, "\nendobj\n");
}

/* Return 1 if the object content has the given /Type name. */
static int
obj_has_type(const unsigned char *c, int len, const char *type_name)
{
    char buf[32] = "";
    get_type_name(c, len, buf, sizeof(buf));
    return strcmp(buf, type_name) == 0;
}

/* ============================================================
 * MERGE
 * ============================================================ */

int pdf_merge_files(const char **input_paths, int count,
                    const char *output_path,
                    char *err_buf, int err_buf_size)
{
    if (count <= 0) {
        snprintf(err_buf, err_buf_size, "No input files.");
        return -1;
    }

    PDF *pdfs = (PDF *)calloc(count, sizeof(PDF));
    if (!pdfs) { snprintf(err_buf, err_buf_size, "Out of memory."); return -1; }

    for (int f = 0; f < count; f++) {
        pdfs[f].data = load_file(input_paths[f], &pdfs[f].size);
        if (!pdfs[f].data) {
            snprintf(err_buf, err_buf_size, "Cannot read: %s", input_paths[f]);
            goto fail;
        }
        if (pdf_parse_full(&pdfs[f]) != 0) {
            snprintf(err_buf, err_buf_size, "Cannot parse PDF: %s", input_paths[f]);
            goto fail;
        }
    }

    /* ---- Build per-PDF obj_maps: old_obj_num → new_obj_num ----
     * Sources: xref entries with real file offsets, plus extracted ObjStm objects.
     * Skip: Catalog, /Pages nodes, ObjStm container objects.
     * The resulting sequential numbering is the authoritative new numbering. */

    int  **obj_maps  = (int **)calloc(count, sizeof(int *));
    int   *map_sizes = (int  *)calloc(count, sizeof(int));
    if (!obj_maps || !map_sizes) {
        snprintf(err_buf, err_buf_size, "Out of memory.");
        goto fail_maps;
    }

    int total_objs = 0;

    for (int f = 0; f < count; f++) {
        PDF *p = &pdfs[f];

        int max_num = p->xref_size;
        for (int e = 0; e < p->extracted_count; e++)
            if (p->extracted[e].obj_num >= max_num)
                max_num = p->extracted[e].obj_num + 1;

        map_sizes[f] = max_num;
        obj_maps[f]  = (int *)calloc(max_num, sizeof(int));
        if (!obj_maps[f]) {
            snprintf(err_buf, err_buf_size, "Out of memory.");
            goto fail_maps;
        }

        /* Assign new numbers to xref-based objects (direct file offset). */
        for (int n = 1; n < p->xref_size; n++) {
            if (p->xref[n].free || p->xref[n].offset == 0) continue;
            if (n == p->catalog_num) continue;
            if (p->is_pages[n]) continue;
            int len; const unsigned char *c = get_obj_content(p, n, &len);
            if (c && obj_has_type(c, len, "ObjStm")) continue;
            obj_maps[f][n] = ++total_objs;
        }

        /* Assign new numbers to extracted objects not already mapped. */
        for (int e = 0; e < p->extracted_count; e++) {
            int on = p->extracted[e].obj_num;
            if (on <= 0 || on >= max_num) continue;
            if (on == p->catalog_num) continue;
            if (on < p->xref_size && p->is_pages[on]) continue;
            if (obj_maps[f][on] > 0) continue;
            obj_maps[f][on] = ++total_objs;
        }
    }

    int new_pages_num   = total_objs + 1;
    int new_catalog_num = total_objs + 2;

    /* ---- Open output file ---- */
    FILE *out = fopen(output_path, "wb");
    if (!out) {
        snprintf(err_buf, err_buf_size, "Cannot create output: %s", output_path);
        goto fail_maps;
    }

    long long *offsets = (long long *)calloc(new_catalog_num + 1, sizeof(long long));
    if (!offsets) {
        fclose(out);
        snprintf(err_buf, err_buf_size, "Out of memory.");
        goto fail_maps;
    }

    fprintf(out, "%%PDF-1.4\n%%%c%c%c%c\n", 0xe2, 0xe3, 0xcf, 0xd3);

    /* ---- Write xref-based objects ---- */
    for (int f = 0; f < count; f++) {
        PDF *p = &pdfs[f];
        for (int n = 1; n < p->xref_size; n++) {
            if (p->xref[n].free || p->xref[n].offset == 0) continue;
            int new_num = obj_maps[f][n];
            if (new_num <= 0) continue;
            int len; const unsigned char *c = get_obj_content(p, n, &len);
            if (!c) continue;
            write_rewritten_obj(out, new_num, c, len,
                                obj_maps[f], map_sizes[f], &offsets[new_num]);
        }
    }

    /* ---- Write extracted (ObjStm) objects ---- */
    for (int f = 0; f < count; f++) {
        PDF *p = &pdfs[f];
        for (int e = 0; e < p->extracted_count; e++) {
            int on = p->extracted[e].obj_num;
            int new_num = (on > 0 && on < map_sizes[f]) ? obj_maps[f][on] : 0;
            if (new_num <= 0) continue;
            write_rewritten_obj(out, new_num,
                                p->extracted[e].buf, p->extracted[e].len,
                                obj_maps[f], map_sizes[f], &offsets[new_num]);
        }
    }

    /* ---- Write combined Pages object ---- */
    offsets[new_pages_num] = ftell(out);
    fprintf(out, "%d 0 obj\n<< /Type /Pages /Kids [", new_pages_num);

    int total_pages = 0;
    for (int f = 0; f < count; f++) {
        for (int pg = 0; pg < pdfs[f].page_count; pg++) {
            int old_pg = pdfs[f].page_nums[pg];
            int new_pg = (old_pg > 0 && old_pg < map_sizes[f]) ? obj_maps[f][old_pg] : 0;
            if (new_pg > 0) { fprintf(out, "%d 0 R ", new_pg); total_pages++; }
        }
    }
    fprintf(out, "] /Count %d >>\nendobj\n", total_pages);

    /* ---- Write Catalog ---- */
    offsets[new_catalog_num] = ftell(out);
    fprintf(out, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n",
            new_catalog_num, new_pages_num);

    /* ---- xref + trailer ---- */
    {
        long long xref_offset = ftell(out);
        int total_entries = new_catalog_num + 1;
        fprintf(out, "xref\n0 %d\n", total_entries);
        fprintf(out, "0000000000 65535 f\r\n");
        for (int i = 1; i < total_entries; i++) {
            if (offsets[i] > 0)
                fprintf(out, "%010lld 00000 n\r\n", offsets[i]);
            else
                fprintf(out, "0000000000 65535 f\r\n");
        }
        fprintf(out, "trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%lld\n%%%%EOF\n",
                total_entries, new_catalog_num, xref_offset);
    }

    fclose(out);
    free(offsets);
    for (int f = 0; f < count; f++) free(obj_maps[f]);
    free(obj_maps);
    free(map_sizes);
    for (int f = 0; f < count; f++) pdf_free(&pdfs[f]);
    free(pdfs);
    return 0;

fail_maps:
    if (obj_maps) { for (int f = 0; f < count; f++) free(obj_maps[f]); free(obj_maps); }
    free(map_sizes);
fail:
    for (int f = 0; f < count; f++) pdf_free(&pdfs[f]);
    free(pdfs);
    return -1;
}

/* ============================================================
 * SPLIT
 * ============================================================ */

int pdf_split_file(const char *input_path, const char *output_dir,
                   char *err_buf, int err_buf_size)
{
    PDF pdf;
    memset(&pdf, 0, sizeof(pdf));

    pdf.data = load_file(input_path, &pdf.size);
    if (!pdf.data) {
        snprintf(err_buf, err_buf_size, "Cannot read: %s", input_path);
        return -1;
    }
    if (pdf_parse_full(&pdf) != 0) {
        snprintf(err_buf, err_buf_size, "Cannot parse PDF: %s", input_path);
        pdf_free(&pdf);
        return -1;
    }

    int n_pages = pdf.page_count;
    if (n_pages == 0) {
        snprintf(err_buf, err_buf_size, "No pages found in PDF.");
        pdf_free(&pdf);
        return -1;
    }

    /* Find the ceiling object number across xref and extracted. */
    int max_num = pdf.xref_size;
    for (int e = 0; e < pdf.extracted_count; e++)
        if (pdf.extracted[e].obj_num >= max_num)
            max_num = pdf.extracted[e].obj_num + 1;

    int new_pages_num   = max_num;
    int new_catalog_num = max_num + 1;
    int total_entries   = new_catalog_num + 1;

    /* Build identity obj_map.  /Pages nodes are redirected to new_pages_num
     * so that /Parent references in Page objects resolve correctly.        */
    int *obj_map = (int *)calloc(max_num, sizeof(int));
    if (!obj_map) {
        snprintf(err_buf, err_buf_size, "Out of memory.");
        pdf_free(&pdf);
        return -1;
    }
    for (int n = 1; n < max_num; n++) {
        if (n == pdf.catalog_num) continue;
        if (n < pdf.xref_size && pdf.is_pages[n])
            obj_map[n] = new_pages_num;
        else
            obj_map[n] = n;
    }

    long long *offsets = (long long *)calloc(total_entries, sizeof(long long));
    if (!offsets) {
        snprintf(err_buf, err_buf_size, "Out of memory.");
        free(obj_map);
        pdf_free(&pdf);
        return -1;
    }

    for (int p = 0; p < n_pages; p++) {
        char out_path[1024];
        snprintf(out_path, sizeof(out_path), "%s\\page_%03d.pdf", output_dir, p + 1);

        FILE *out = fopen(out_path, "wb");
        if (!out) {
            snprintf(err_buf, err_buf_size, "Cannot create: %s", out_path);
            free(obj_map);
            free(offsets);
            pdf_free(&pdf);
            return -1;
        }

        memset(offsets, 0, total_entries * sizeof(long long));

        fprintf(out, "%%PDF-1.4\n%%%c%c%c%c\n", 0xe2, 0xe3, 0xcf, 0xd3);

        /* Write xref-based objects (keep original numbers). */
        for (int n = 1; n < pdf.xref_size; n++) {
            if (pdf.xref[n].free || pdf.xref[n].offset == 0) continue;
            if (n == pdf.catalog_num) continue;
            if (pdf.is_pages[n]) continue;
            int len; const unsigned char *c = get_obj_content(&pdf, n, &len);
            if (!c || obj_has_type(c, len, "ObjStm")) continue;
            write_rewritten_obj(out, n, c, len, obj_map, max_num, &offsets[n]);
        }

        /* Write extracted objects (keep original numbers, skip duplicates). */
        for (int e = 0; e < pdf.extracted_count; e++) {
            int on = pdf.extracted[e].obj_num;
            if (on <= 0 || on >= max_num) continue;
            if (on == pdf.catalog_num) continue;
            if (on < pdf.xref_size && pdf.is_pages[on]) continue;
            if (offsets[on] > 0) continue; /* already written via xref */
            write_rewritten_obj(out, on,
                                pdf.extracted[e].buf, pdf.extracted[e].len,
                                obj_map, max_num, &offsets[on]);
        }

        /* New Pages object for this single page only. */
        int page_num = pdf.page_nums[p];
        offsets[new_pages_num] = ftell(out);
        fprintf(out, "%d 0 obj\n"
                "<< /Type /Pages /Kids [%d 0 R] /Count 1 >>\nendobj\n",
                new_pages_num, page_num);

        /* New Catalog. */
        offsets[new_catalog_num] = ftell(out);
        fprintf(out, "%d 0 obj\n"
                "<< /Type /Catalog /Pages %d 0 R >>\nendobj\n",
                new_catalog_num, new_pages_num);

        /* xref table. */
        long long xref_offset = ftell(out);
        fprintf(out, "xref\n0 %d\n", total_entries);
        fprintf(out, "0000000000 65535 f\r\n");
        for (int i = 1; i < total_entries; i++) {
            if (offsets[i] > 0)
                fprintf(out, "%010lld 00000 n\r\n", offsets[i]);
            else
                fprintf(out, "0000000000 65535 f\r\n");
        }
        fprintf(out, "trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%lld\n%%%%EOF\n",
                total_entries, new_catalog_num, xref_offset);

        fclose(out);
    }

    free(obj_map);
    free(offsets);
    pdf_free(&pdf);
    return n_pages;
}
