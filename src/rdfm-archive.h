/*
 * rdfm-archive.h  —  Archive support for rdfm
 *
 * Backend priority:  7z (7za/7zz)  →  zip+tar  fallback  →  unrar (RAR only)
 *
 * Copyright (C) 2024  rdfm contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RDFM_ARCHIVE_H
#define RDFM_ARCHIVE_H

#include <gtk/gtk.h>
#include <libfm/fm.h>
#include <libfm/fm-gtk.h>

/* ── kill-switch ─────────────────────────────────────────────────────────── */

/*
 * Set to FALSE before gtk_main() to disable the whole subsystem.
 * Controlled by the --no-archive command-line flag (parsed in rdfm.c).
 * When FALSE:
 *   • rdfm_is_archive() always returns FALSE (archives open normally)
 *   • all dialog functions return immediately without doing anything
 */
extern gboolean rdfm_archive_enabled;

/* ── type enum ───────────────────────────────────────────────────────────── */

typedef enum {
    RDFM_ARCHIVE_UNKNOWN = 0,
    RDFM_ARCHIVE_TAR,         /* .tar           */
    RDFM_ARCHIVE_TAR_GZ,      /* .tar.gz / .tgz */
    RDFM_ARCHIVE_TAR_BZ2,     /* .tar.bz2       */
    RDFM_ARCHIVE_TAR_XZ,      /* .tar.xz        */
    RDFM_ARCHIVE_TAR_ZST,     /* .tar.zst       */
    RDFM_ARCHIVE_TAR_LZ4,     /* .tar.lz4       */
    RDFM_ARCHIVE_GZ,          /* .gz  (single)  */
    RDFM_ARCHIVE_BZ2,         /* .bz2 (single)  */
    RDFM_ARCHIVE_XZ,          /* .xz  (single)  */
    RDFM_ARCHIVE_ZIP,         /* .zip / .jar    */
    RDFM_ARCHIVE_7Z,          /* .7z            */
    RDFM_ARCHIVE_RAR,         /* .rar           */
    RDFM_ARCHIVE_ZSTD,        /* .zst (single)  */
} RdfmArchiveType;

/* ── detection ───────────────────────────────────────────────────────────── */

RdfmArchiveType rdfm_archive_detect(const char *filename);
gboolean        rdfm_is_archive(const char *filename);

/* ── tool probes ─────────────────────────────────────────────────────────── */

gboolean rdfm_have_7z(void);
gboolean rdfm_have_rar(void);

/* ── operations ──────────────────────────────────────────────────────────── */

/*
 * rdfm_archive_view()
 *
 * Show archive contents in a searchable list dialog with an Extract button.
 * parent   : transient parent
 * filepath : absolute path to the archive
 */
void rdfm_archive_view(GtkWindow *parent, const char *filepath);

/*
 * rdfm_archive_extract()
 *
 * Show the extract-to dialog, then extract.
 * parent    : transient parent
 * filepath  : absolute path to the archive
 * cwd_hint  : directory the user is currently browsing — becomes the default
 *             extraction destination.  Pass NULL to fall back to the archive's
 *             own directory.
 */
void rdfm_archive_extract(GtkWindow *parent, const char *filepath,
                          const char *cwd_hint);

/*
 * rdfm_archive_create()
 *
 * Show the create-archive dialog and spawn the archiver.
 * parent : transient parent
 * files  : selected source files
 * cwd    : directory the user is currently browsing — used as the default
 *          save location for the output archive.
 *
 * The archiver is always invoked in the common parent directory of the
 * selected files so that paths inside the archive are RELATIVE.
 */
void rdfm_archive_create(GtkWindow *parent, FmPathList *files,
                         const char *cwd);

/* ── convenience wrappers ────────────────────────────────────────────────── */

/*
 * rdfm_archive_extract_files()
 *
 * Extract every archive in `files`.  cwd is passed as cwd_hint so extracts
 * land in the current view, not in the archives' source directories.
 */
void rdfm_archive_extract_files(GtkWindow *parent, FmFileInfoList *files,
                                const char *cwd);

/*
 * rdfm_archive_view_file()
 *
 * Open the archive viewer for a single FmFileInfo entry.
 */
void rdfm_archive_view_file(GtkWindow *parent, FmFileInfo *fi);

#endif /* RDFM_ARCHIVE_H */
