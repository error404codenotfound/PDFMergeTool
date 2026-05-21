#ifndef PDF_MERGE_H
#define PDF_MERGE_H

/* Merge input_paths[0..count-1] into output_path.
   Files must already be sorted in caller's desired order.
   Returns 0 on success. err_buf receives a human-readable message on failure. */
int pdf_merge_files(const char **input_paths, int count,
                    const char *output_path,
                    char *err_buf, int err_buf_size);

/* Split input_path into individual pages written to output_dir.
   Each page is named  "<base_name> pg 001.pdf", "<base_name> pg 002.pdf", …
   Pass NULL or "" for base_name to use the source filename (without .pdf).
   Returns page count on success, -1 on error. */
int pdf_split_file(const char *input_path, const char *output_dir,
                   const char *base_name,
                   char *err_buf, int err_buf_size);

#endif /* PDF_MERGE_H */
