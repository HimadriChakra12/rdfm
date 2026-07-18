#ifndef RDFM_ARCHIVE_H
#define RDFM_ARCHIVE_H

#include <gtk/gtk.h>
#include <libfm/fm.h>
#include <libfm/fm-gtk.h>

/* Archive type detection */
typedef enum {
    RDFM_ARCHIVE_UNKNOWN = 0,
    RDFM_ARCHIVE_TAR,         /* .tar */
    RDFM_ARCHIVE_TAR_GZ,      /* .tar.gz / .tgz */
    RDFM_ARCHIVE_TAR_BZ2,     /* .tar.bz2 / .tbz2 */
    RDFM_ARCHIVE_TAR_XZ,      /* .tar.xz / .txz */
    RDFM_ARCHIVE_TAR_ZST,     /* .tar.zst */
    RDFM_ARCHIVE_TAR_LZ4,     /* .tar.lz4 */
    RDFM_ARCHIVE_GZ,          /* .gz  (single file) */
    RDFM_ARCHIVE_BZ2,         /* .bz2 (single file) */
    RDFM_ARCHIVE_XZ,          /* .xz  (single file) */
    RDFM_ARCHIVE_ZIP,         /* .zip */
    RDFM_ARCHIVE_7Z,          /* .7z  */
    RDFM_ARCHIVE_RAR,         /* .rar */
    RDFM_ARCHIVE_ZSTD,        /* .zst (single file) */
} RdfmArchiveType;

/* Detect archive type from filename */
RdfmArchiveType rdfm_archive_detect(const char *filename);

/* Returns TRUE if the filename looks like a supported archive */
gboolean rdfm_is_archive(const char *filename);

/* Returns TRUE if 7z is available on PATH */
gboolean rdfm_have_7z(void);

/* Returns TRUE if rar/unrar is available on PATH */
gboolean rdfm_have_rar(void);

/*
 * View archive contents in a GTK dialog (file list + icon + size).
 * parent   - transient parent window
 * filepath - absolute path to the archive
 */
void rdfm_archive_view(GtkWindow *parent, const char *filepath);

/*
 * Extract archive.
 * parent   - transient parent window
 * filepath - absolute path to the archive
 * dest_dir - destination directory (NULL = ask user)
 *
 * Shows a destination chooser by default.  If the archive is encrypted
 * a password entry dialog is shown first.
 */
void rdfm_archive_extract(GtkWindow *parent, const char *filepath,
                          const char *dest_dir);

/*
 * Create a new archive from a list of files/directories.
 * parent   - transient parent window
 * files    - FmPathList of source paths
 * cwd      - current directory (used as default output location)
 */
void rdfm_archive_create(GtkWindow *parent, FmPathList *files,
                         const char *cwd);

/* Convenience: called from context menu with selected FmFileInfoList */
void rdfm_archive_extract_files(GtkWindow *parent, FmFileInfoList *files);
void rdfm_archive_view_file(GtkWindow *parent, FmFileInfo *fi);

#endif /* RDFM_ARCHIVE_H */
