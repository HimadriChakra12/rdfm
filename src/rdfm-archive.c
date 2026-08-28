/*
 * rdfm-archive.c  —  Archive support for rdfm
 *
 * Backend priority
 * ────────────────
 *   Create / Extract / List:  7z (7za / 7zz)  →  zip+unzip / tar  fallback
 *   RAR extract only:         unrar / rar
 *
 * Key fixes over the previous version
 * ─────────────────────────────────────
 *   • Full-path bug: archives are now created with RELATIVE paths.
 *     The spawner chdirs into the common parent of the selected files,
 *     then passes bare filenames.  Extracting into ~/Photos no longer
 *     recreates the full /home/user/Downloads/… tree.
 *
 *   • Extract destination defaults to the CURRENT WORKING DIRECTORY
 *     (where the user is browsing), not to the directory containing the
 *     archive.
 *
 *   • 7z is tried first for all types it can handle.  zip/unzip and tar
 *     are only used when 7z is absent.
 *
 *   • --no-archive flag: set rdfm_archive_enabled = FALSE before main()
 *     runs to disable the whole subsystem cleanly.
 *
 * Copyright (C) 2024  rdfm contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "rdfm-archive.h"

#include <string.h>
#include <errno.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libfm/fm.h>
#include <libfm/fm-gtk.h>

/* ── global kill-switch (set by --no-archive) ────────────────────────────── */

gboolean rdfm_archive_enabled = TRUE;

/* ── tool detection ──────────────────────────────────────────────────────── */

static const char *_7z_bin(void)
{
    static const char *bin = NULL;
    if (!bin) {
        if      (g_find_program_in_path("7z"))   bin = "7z";
        else if (g_find_program_in_path("7za"))  bin = "7za";
        else if (g_find_program_in_path("7zz"))  bin = "7zz";
        /* leave NULL if none found */
    }
    return bin;
}

gboolean rdfm_have_7z(void)
{
    return _7z_bin() != NULL;
}

gboolean rdfm_have_rar(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (g_find_program_in_path("unrar") ||
                  g_find_program_in_path("rar")) ? 1 : 0;
    return (gboolean)cached;
}

static const char *_unrar_bin(void)
{
    static const char *bin = NULL;
    if (!bin) {
        if      (g_find_program_in_path("unrar")) bin = "unrar";
        else if (g_find_program_in_path("rar"))   bin = "rar";
    }
    return bin;
}

/* Is zip(1) available for creation? */
static gboolean _have_zip(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = g_find_program_in_path("zip") ? 1 : 0;
    return (gboolean)cached;
}

/* Is tar(1) available? */
static gboolean _have_tar(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = g_find_program_in_path("tar") ? 1 : 0;
    return (gboolean)cached;
}

/* ── type detection ──────────────────────────────────────────────────────── */

RdfmArchiveType rdfm_archive_detect(const char *name)
{
    if (!name) return RDFM_ARCHIVE_UNKNOWN;
    if (g_str_has_suffix(name, ".tar.gz")  ||
        g_str_has_suffix(name, ".tgz"))       return RDFM_ARCHIVE_TAR_GZ;
    if (g_str_has_suffix(name, ".tar.bz2") ||
        g_str_has_suffix(name, ".tbz2")    ||
        g_str_has_suffix(name, ".tbz"))       return RDFM_ARCHIVE_TAR_BZ2;
    if (g_str_has_suffix(name, ".tar.xz")  ||
        g_str_has_suffix(name, ".txz"))       return RDFM_ARCHIVE_TAR_XZ;
    if (g_str_has_suffix(name, ".tar.zst") ||
        g_str_has_suffix(name, ".tzst"))      return RDFM_ARCHIVE_TAR_ZST;
    if (g_str_has_suffix(name, ".tar.lz4"))   return RDFM_ARCHIVE_TAR_LZ4;
    if (g_str_has_suffix(name, ".tar"))       return RDFM_ARCHIVE_TAR;
    if (g_str_has_suffix(name, ".zip")  ||
        g_str_has_suffix(name, ".jar")  ||
        g_str_has_suffix(name, ".apk")  ||
        g_str_has_suffix(name, ".war"))       return RDFM_ARCHIVE_ZIP;
    if (g_str_has_suffix(name, ".7z"))        return RDFM_ARCHIVE_7Z;
    if (g_str_has_suffix(name, ".rar"))       return RDFM_ARCHIVE_RAR;
    if (g_str_has_suffix(name, ".gz"))        return RDFM_ARCHIVE_GZ;
    if (g_str_has_suffix(name, ".bz2"))       return RDFM_ARCHIVE_BZ2;
    if (g_str_has_suffix(name, ".xz"))        return RDFM_ARCHIVE_XZ;
    if (g_str_has_suffix(name, ".zst"))       return RDFM_ARCHIVE_ZSTD;
    return RDFM_ARCHIVE_UNKNOWN;
}

gboolean rdfm_is_archive(const char *filename)
{
    if (!rdfm_archive_enabled) return FALSE;
    return rdfm_archive_detect(filename) != RDFM_ARCHIVE_UNKNOWN;
}

/* ── generic helpers ─────────────────────────────────────────────────────── */

/* Run argv synchronously in working dir `wd` (NULL = inherit).
 * Returns stdout text (caller frees) and sets *exit_status. */
static char *_run_capture(const char **argv, const char *wd, int *exit_status)
{
    char *out = NULL;
    GError *err = NULL;
    g_spawn_sync(wd, (char **)argv, NULL,
                 G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL,
                 NULL, NULL, &out, NULL, exit_status, &err);
    if (err) { g_error_free(err); return NULL; }
    return out;
}

static void _err_dialog(GtkWindow *parent, const char *msg)
{
    GtkWidget *d = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static char *_ask_password(GtkWindow *parent, const char *arc_name)
{
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Password Required"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_OK",     GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);

    char *markup = g_markup_printf_escaped(
        "<b>%s</b>\n<small>%s</small>",
        _("Archive is password-protected"), arc_name);
    GtkWidget *lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(lbl), markup);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(box), lbl, FALSE, FALSE, 0);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    char *pw = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_OK)
        pw = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    gtk_widget_destroy(dlg);
    return pw;
}

/* Strip known archive extensions → bare basename for default naming */
static char *_strip_ext(const char *basename)
{
    static const char *exts[] = {
        ".tar.gz", ".tar.bz2", ".tar.xz", ".tar.zst", ".tar.lz4",
        ".tgz", ".tbz2", ".tbz", ".txz", ".tzst",
        ".tar", ".zip", ".7z", ".rar", ".gz", ".bz2", ".xz", ".zst", NULL
    };
    for (int i = 0; exts[i]; i++)
        if (g_str_has_suffix(basename, exts[i]))
            return g_strndup(basename, strlen(basename) - strlen(exts[i]));
    return g_strdup(basename);
}

/* Does 7z output suggest wrong/missing password? */
static gboolean _looks_like_pw_error(int xst, const char *out)
{
    if (xst == 0) return FALSE;
    if (!out) return TRUE;
    return (strstr(out, "password") || strstr(out, "encrypted") ||
            strstr(out, "Wrong password") || strstr(out, "Cannot open") ||
            strstr(out, "Data Error"));
}

/* ── LIST ────────────────────────────────────────────────────────────────── */
/*
 * Priority: 7z for everything it can open → tar for tarballs → unzip for zip
 * → unrar for rar.
 */

static GPtrArray *_list_argv(RdfmArchiveType t, const char *path,
                             const char *pw)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);

    /* Use 7z whenever available — it handles tar, zip, gz, bz2, xz, 7z, zst */
    gboolean use_7z = rdfm_have_7z() && (t != RDFM_ARCHIVE_RAR || !rdfm_have_rar());

    if (use_7z && t != RDFM_ARCHIVE_RAR) {
        g_ptr_array_add(a, g_strdup(_7z_bin()));
        g_ptr_array_add(a, g_strdup("l"));
        g_ptr_array_add(a, g_strdup("-ba"));   /* bare: no header/footer */
        g_ptr_array_add(a, g_strdup("-slt"));  /* technical info per entry */
        if (pw) g_ptr_array_add(a, g_strconcat("-p", pw, NULL));
        g_ptr_array_add(a, g_strdup(path));
        g_ptr_array_add(a, NULL);
        return a;
    }

    switch (t) {
    /* tar fallback (no 7z) */
    case RDFM_ARCHIVE_TAR:
    case RDFM_ARCHIVE_TAR_GZ:
    case RDFM_ARCHIVE_TAR_BZ2:
    case RDFM_ARCHIVE_TAR_XZ:
    case RDFM_ARCHIVE_TAR_ZST:
    case RDFM_ARCHIVE_TAR_LZ4:
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--list"));
        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(path));
        break;
    /* zip fallback */
    case RDFM_ARCHIVE_ZIP:
        g_ptr_array_add(a, g_strdup("unzip"));
        g_ptr_array_add(a, g_strdup("-Z1"));
        if (pw) {
            g_ptr_array_add(a, g_strdup("-P"));
            g_ptr_array_add(a, g_strdup(pw));
        }
        g_ptr_array_add(a, g_strdup(path));
        break;
    /* rar */
    case RDFM_ARCHIVE_RAR:
        g_ptr_array_add(a, g_strdup(_unrar_bin()));
        g_ptr_array_add(a, g_strdup("lb"));
        if (pw) g_ptr_array_add(a, g_strconcat("-p", pw, NULL));
        g_ptr_array_add(a, g_strdup(path));
        break;
    default:
        break;
    }
    g_ptr_array_add(a, NULL);
    return a;
}

/*
 * Parse 7z -slt output into a plain name list.
 * -slt emits blocks like:
 *   ----------
 *   Path = somefile.txt
 *   ...
 */
static GList *_parse_7z_slt(const char *output)
{
    if (!output) return NULL;
    GList *list = NULL;
    char **lines = g_strsplit(output, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (g_str_has_prefix(line, "Path = ")) {
            const char *name = line + 7;  /* skip "Path = " */
            if (*name)
                list = g_list_prepend(list, g_strdup(name));
        }
    }
    g_strfreev(lines);
    return g_list_reverse(list);
}

static GList *_parse_listing(RdfmArchiveType t, const char *output,
                             gboolean used_7z)
{
    if (!output || !*output) return NULL;

    if (used_7z)
        return _parse_7z_slt(output);

    /* tar / unzip / unrar: one entry per line */
    GList *list = NULL;
    char **lines = g_strsplit(output, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (*line)
            list = g_list_prepend(list, g_strdup(line));
    }
    g_strfreev(lines);
    return g_list_reverse(list);
}

/* ── VIEW DIALOG ─────────────────────────────────────────────────────────── */

enum { COL_ICON = 0, COL_NAME, COL_N };

static gboolean _row_visible(GtkTreeModel *model, GtkTreeIter *iter,
                             gpointer data)
{
    const char *text = gtk_entry_get_text(GTK_ENTRY(data));
    if (!text || !*text) return TRUE;
    char *name = NULL;
    gtk_tree_model_get(model, iter, COL_NAME, &name, -1);
    gboolean vis = name && (strcasestr(name, text) != NULL);
    g_free(name);
    return vis;
}

static void _on_search_changed(GtkSearchEntry *se, gpointer filter)
{
    (void)se;
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
}

void rdfm_archive_view(GtkWindow *parent, const char *filepath)
{
    if (!rdfm_archive_enabled) return;

    const char *basename = g_path_get_basename(filepath);
    RdfmArchiveType type  = rdfm_archive_detect(basename);

    if (type == RDFM_ARCHIVE_UNKNOWN) {
        _err_dialog(parent, _("Unsupported archive format.")); return;
    }
    if (type == RDFM_ARCHIVE_RAR && !rdfm_have_rar() && !rdfm_have_7z()) {
        _err_dialog(parent, _("Install p7zip or unrar to open .rar archives."));
        return;
    }
    if (type == RDFM_ARCHIVE_7Z && !rdfm_have_7z()) {
        _err_dialog(parent, _("Install p7zip to open .7z archives.")); return;
    }

    gboolean used_7z = rdfm_have_7z() && (type != RDFM_ARCHIVE_RAR || !rdfm_have_rar());

    char *password = NULL;
    GList *entries = NULL;

retry:;
    GPtrArray *argv = _list_argv(type, filepath, password);
    int xst = 0;
    char *out = _run_capture((const char **)argv->pdata, NULL, &xst);
    g_ptr_array_free(argv, TRUE);

    if (_looks_like_pw_error(xst, out) && !password) {
        g_free(out);
        password = _ask_password(parent, basename);
        if (!password) return;
        goto retry;
    }

    entries = _parse_listing(type, out, used_7z);
    g_free(out);

    if (!entries) {
        _err_dialog(parent, _("Could not read archive contents."));
        g_free(password);
        return;
    }

    /* ── build list store ── */
    GtkListStore *store = gtk_list_store_new(COL_N,
                              GDK_TYPE_PIXBUF, G_TYPE_STRING);
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    for (GList *l = entries; l; l = l->next) {
        const char *name = l->data;
        const char *icon_name =
            g_str_has_suffix(name, "/")     ? "folder"          :
            (g_str_has_suffix(name, ".png") ||
             g_str_has_suffix(name, ".jpg") ||
             g_str_has_suffix(name, ".jpeg")||
             g_str_has_suffix(name, ".svg") ||
             g_str_has_suffix(name, ".webp"))? "image-x-generic" :
            (g_str_has_suffix(name, ".sh")  ||
             g_str_has_suffix(name, ".py")  ||
             g_str_has_suffix(name, ".c")   ||
             g_str_has_suffix(name, ".cpp") ||
             g_str_has_suffix(name, ".lua"))? "text-x-script"   :
                                              "text-x-generic";
        GdkPixbuf *pb = gtk_icon_theme_load_icon(theme, icon_name, 16,
                             GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, COL_ICON, pb, COL_NAME, name, -1);
        if (pb) g_object_unref(pb);
    }

    /* ── filter + search ── */
    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_set_tooltip_text(search, _("Filter entries"));

    GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER(
        gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL));
    g_object_unref(store);
    gtk_tree_model_filter_set_visible_func(filter, _row_visible, search, NULL);
    g_signal_connect(search, "search-changed",
                     G_CALLBACK(_on_search_changed), filter);

    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(filter));
    g_object_unref(filter);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), FALSE);

    GtkCellRenderer *ri = gtk_cell_renderer_pixbuf_new();
    GtkCellRenderer *rt = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new();
    gtk_tree_view_column_pack_start(col, ri, FALSE);
    gtk_tree_view_column_add_attribute(col, ri, "pixbuf", COL_ICON);
    gtk_tree_view_column_pack_start(col, rt, TRUE);
    gtk_tree_view_column_add_attribute(col, rt, "text",   COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

    int n_entries = g_list_length(entries);
    char *cstr = g_strdup_printf(
        ngettext("%d entry", "%d entries", n_entries), n_entries);
    GtkWidget *count_lbl = gtk_label_new(cstr);
    gtk_label_set_xalign(GTK_LABEL(count_lbl), 0.0);
    g_free(cstr);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
                                        GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(scroll), view);

    char *title = g_strdup_printf("%s", basename);
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        title, parent, GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Extract…", GTK_RESPONSE_APPLY,
        "_Close",    GTK_RESPONSE_CLOSE, NULL);
    g_free(title);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 580, 480);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);
    gtk_box_set_spacing(GTK_BOX(box), 6);
    gtk_box_pack_start(GTK_BOX(box), search,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scroll,    TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(box), count_lbl, FALSE, FALSE, 0);
    gtk_widget_show_all(dlg);

    gint resp = gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);

    if (resp == GTK_RESPONSE_APPLY)
        rdfm_archive_extract(parent, filepath, NULL);

    g_list_free_full(entries, g_free);
    g_free(password);
}

void rdfm_archive_view_file(GtkWindow *parent, FmFileInfo *fi)
{
    if (!rdfm_archive_enabled) return;
    char *path = fm_path_to_str(fm_file_info_get_path(fi));
    rdfm_archive_view(parent, path);
    g_free(path);
}

/* ── EXTRACT ─────────────────────────────────────────────────────────────── */
/*
 * Priority: 7z x  →  tar  →  unzip  →  unrar
 *
 * dest is always an absolute path decided by the dialog (defaults to cwd,
 * i.e. where the user is browsing — not where the archive file lives).
 */

static GPtrArray *_extract_argv(RdfmArchiveType t, const char *path,
                                const char *dest, const char *pw)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);

    gboolean use_7z = rdfm_have_7z() && (t != RDFM_ARCHIVE_RAR || !rdfm_have_rar());

    if (use_7z && t != RDFM_ARCHIVE_RAR) {
        g_ptr_array_add(a, g_strdup(_7z_bin()));
        g_ptr_array_add(a, g_strdup("x"));
        g_ptr_array_add(a, g_strdup("-y"));
        if (pw) g_ptr_array_add(a, g_strconcat("-p", pw, NULL));
        g_ptr_array_add(a, g_strdup(path));
        g_ptr_array_add(a, g_strconcat("-o", dest, NULL));
        g_ptr_array_add(a, NULL);
        return a;
    }

    switch (t) {
    case RDFM_ARCHIVE_TAR:
    case RDFM_ARCHIVE_TAR_GZ:
    case RDFM_ARCHIVE_TAR_BZ2:
    case RDFM_ARCHIVE_TAR_XZ:
    case RDFM_ARCHIVE_TAR_ZST:
    case RDFM_ARCHIVE_TAR_LZ4:
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--extract"));
        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(path));
        g_ptr_array_add(a, g_strdup("--directory"));
        g_ptr_array_add(a, g_strdup(dest));
        break;
    case RDFM_ARCHIVE_ZIP:
        g_ptr_array_add(a, g_strdup("unzip"));
        g_ptr_array_add(a, g_strdup("-o"));
        if (pw) {
            g_ptr_array_add(a, g_strdup("-P"));
            g_ptr_array_add(a, g_strdup(pw));
        }
        g_ptr_array_add(a, g_strdup(path));
        g_ptr_array_add(a, g_strdup("-d"));
        g_ptr_array_add(a, g_strdup(dest));
        break;
    case RDFM_ARCHIVE_RAR:
        g_ptr_array_add(a, g_strdup(_unrar_bin()));
        g_ptr_array_add(a, g_strdup("x"));
        g_ptr_array_add(a, g_strdup("-y"));
        if (pw) g_ptr_array_add(a, g_strconcat("-p", pw, NULL));
        g_ptr_array_add(a, g_strdup(path));
        g_ptr_array_add(a, g_strdup(dest));
        g_ptr_array_add(a, g_strdup("/"));
        break;
    default: break;
    }
    g_ptr_array_add(a, NULL);
    return a;
}

/*
 * rdfm_archive_extract()
 *
 * cwd_hint  — the directory the user is currently browsing.
 *             This becomes the default extraction destination.
 *             If NULL, falls back to the directory of the archive file.
 */
void rdfm_archive_extract(GtkWindow *parent, const char *filepath,
                          const char *cwd_hint)
{
    if (!rdfm_archive_enabled) return;

    const char *basename = g_path_get_basename(filepath);
    RdfmArchiveType type  = rdfm_archive_detect(basename);

    if (type == RDFM_ARCHIVE_UNKNOWN) {
        _err_dialog(parent, _("Unsupported archive format.")); return;
    }
    if (type == RDFM_ARCHIVE_7Z && !rdfm_have_7z()) {
        _err_dialog(parent, _("Install p7zip to extract .7z archives.")); return;
    }
    if (type == RDFM_ARCHIVE_RAR && !rdfm_have_rar() && !rdfm_have_7z()) {
        _err_dialog(parent, _("Install p7zip or unrar to extract .rar archives."));
        return;
    }

    char *archive_dir  = g_path_get_dirname(filepath);
    char *base_noext   = _strip_ext(basename);

    /* Default destination = CWD (where user is browsing), not archive location */
    const char *default_base = cwd_hint ? cwd_hint : archive_dir;

    /* ── destination dialog ── */
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Extract Archive"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",  GTK_RESPONSE_CANCEL,
        "_Extract", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 480, -1);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);

    /* title */
    char *markup = g_markup_printf_escaped(
        "<b>%s</b>  <small>%s</small>",
        _("Extract:"), basename);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    /* separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 2);

    /* folder chooser — defaults to cwd_hint (where user is browsing) */
    GtkWidget *dest_lbl = gtk_label_new(_("Extract to:"));
    gtk_label_set_xalign(GTK_LABEL(dest_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(box), dest_lbl, FALSE, FALSE, 0);

    GtkWidget *chooser = gtk_file_chooser_button_new(
        _("Choose Destination"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser), default_base);
    gtk_box_pack_start(GTK_BOX(box), chooser, FALSE, FALSE, 0);

    /* subfolder option */
    GtkWidget *sub_check = gtk_check_button_new_with_label(
        _("Extract into subfolder:"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sub_check), TRUE);
    gtk_box_pack_start(GTK_BOX(box), sub_check, FALSE, FALSE, 0);

    GtkWidget *sub_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(sub_entry), base_noext);
    gtk_entry_set_activates_default(GTK_ENTRY(sub_entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), sub_entry, FALSE, FALSE, 0);

    /* sensitivity: grey sub_entry when sub_check is off */
    g_signal_connect_swapped(sub_check, "toggled",
        G_CALLBACK(gtk_widget_set_sensitive), sub_entry);

    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_OK) {
        gtk_widget_destroy(dlg);
        g_free(base_noext); g_free(archive_dir);
        return;
    }

    char *chosen   = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    gboolean use_sub = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sub_check));
    const char *sub  = gtk_entry_get_text(GTK_ENTRY(sub_entry));
    char *dest = (use_sub && sub && *sub)
        ? g_build_filename(chosen, sub, NULL)
        : g_strdup(chosen);
    gtk_widget_destroy(dlg);
    g_free(chosen);

    if (g_mkdir_with_parents(dest, 0755) != 0) {
        char *msg = g_strdup_printf(_("Could not create: %s\n%s"),
                                    dest, g_strerror(errno));
        _err_dialog(parent, msg);
        g_free(msg); g_free(dest);
        g_free(base_noext); g_free(archive_dir);
        return;
    }

    char *password = NULL;
do_extract:;
    GPtrArray *argv = _extract_argv(type, filepath, dest, password);
    int xst = 0;
    char *out = _run_capture((const char **)argv->pdata, NULL, &xst);
    g_ptr_array_free(argv, TRUE);

    if (_looks_like_pw_error(xst, out) && !password) {
        g_free(out);
        password = _ask_password(parent, basename);
        if (password) goto do_extract;
    } else if (xst != 0) {
        char *msg = g_strdup_printf(
            _("Extraction failed.\n\n%s"),
            out ? out : _("The archive may be corrupted or require a password."));
        _err_dialog(parent, msg);
        g_free(msg);
    }
    g_free(out);

    g_free(password);
    g_free(dest);
    g_free(base_noext);
    g_free(archive_dir);
}

void rdfm_archive_extract_files(GtkWindow *parent, FmFileInfoList *files,
                                const char *cwd)
{
    if (!rdfm_archive_enabled) return;
    for (GList *l = fm_file_info_list_peek_head_link(files); l; l = l->next) {
        FmFileInfo *fi = l->data;
        if (!rdfm_is_archive(fm_file_info_get_name(fi))) continue;
        char *path = fm_path_to_str(fm_file_info_get_path(fi));
        rdfm_archive_extract(parent, path, cwd);
        g_free(path);
    }
}

/* ── CREATE ──────────────────────────────────────────────────────────────── */
/*
 * Full-path fix
 * ─────────────
 * Old code:  zip myarchive.zip /home/user/Downloads/file.txt
 *   → unzips into  /home/user/Downloads/file.txt  (absolute path inside zip)
 *
 * New code:  cd /home/user/Downloads && zip myarchive.zip file.txt
 *   → unzips as   file.txt  (relative, correct)
 *
 * Implementation: find the common parent dir of all selected files.
 * Spawn the archiver with that dir as working directory.
 * Pass only the basenames (or paths relative to that common parent).
 */

/* Find the longest common directory prefix of a list of absolute paths. */
static char *_common_parent(GList *paths)
{
    if (!paths) return NULL;

    /* Start with the dirname of the first path */
    char *base = g_path_get_dirname((const char *)paths->data);

    for (GList *l = paths->next; l; l = l->next) {
        char *dir = g_path_get_dirname((const char *)l->data);
        /* Trim base until it is a prefix of dir */
        while (!g_str_has_prefix(dir, base) ||
               (dir[strlen(base)] != '\0' && dir[strlen(base)] != '/'))
        {
            char *parent = g_path_get_dirname(base);
            if (g_strcmp0(parent, base) == 0) { /* reached root */
                g_free(parent); g_free(dir);
                g_free(base);
                return g_strdup("/");
            }
            g_free(base);
            base = parent;
        }
        g_free(dir);
    }
    return base;  /* caller frees */
}

/* Given an absolute path and a parent dir, return the relative subpath.
 * e.g.  /home/u/Downloads/foo  ,  /home/u/Downloads  →  foo */
static char *_rel_path(const char *abs, const char *parent)
{
    size_t plen = strlen(parent);
    if (strncmp(abs, parent, plen) == 0) {
        const char *rel = abs + plen;
        while (*rel == '/') rel++;
        return (*rel) ? g_strdup(rel) : g_strdup(".");
    }
    return g_strdup(abs); /* shouldn't happen, fallback */
}

typedef struct {
    const char *label;
    const char *ext;
    gboolean    need_7z;
    gboolean    need_rar;
    gboolean    need_zip; /* needs standalone zip(1) */
} ArchiveFmt;

static const ArchiveFmt FORMATS[] = {
    /* 7z-native formats — best choice when 7z is present */
    { "7z   — 7zip (best compression)",    ".7z",      TRUE,  FALSE, FALSE },
    { "zip  — via 7z",                     ".zip",     TRUE,  FALSE, FALSE },
    { "tar.xz  — xz compressed",           ".tar.xz",  FALSE, FALSE, FALSE },
    { "tar.gz  — gzip compressed",         ".tar.gz",  FALSE, FALSE, FALSE },
    { "tar.bz2 — bzip2 compressed",        ".tar.bz2", FALSE, FALSE, FALSE },
    { "tar.zst — zstd compressed",         ".tar.zst", FALSE, FALSE, FALSE },
    { "tar     — uncompressed",            ".tar",     FALSE, FALSE, FALSE },
    /* zip via standalone zip(1) — shown when 7z is absent */
    { "zip  — via zip(1)",                 ".zip",     FALSE, FALSE, TRUE  },
    { NULL, NULL, FALSE, FALSE, FALSE }
};

static int _avail_formats[G_N_ELEMENTS(FORMATS)];
static int _n_avail = 0;

static void _build_avail(void)
{
    _n_avail = 0;
    gboolean have7z  = rdfm_have_7z();
    gboolean havezip = _have_zip();

    for (int i = 0; FORMATS[i].label; i++) {
        if (FORMATS[i].need_7z  && !have7z)  continue;
        if (FORMATS[i].need_rar && !rdfm_have_rar()) continue;
        /* skip standalone-zip entry when 7z is present (7z handles zip too) */
        if (FORMATS[i].need_zip && have7z)   continue;
        if (FORMATS[i].need_zip && !havezip) continue;
        /* skip tar variants when neither 7z nor tar is available */
        if (!FORMATS[i].need_7z && !FORMATS[i].need_zip &&
            !have7z && !_have_tar()) continue;
        _avail_formats[_n_avail++] = i;
    }
}

static void _on_fmt_changed(GtkComboBox *cb, gpointer data)
{
    GtkEntry *entry = GTK_ENTRY(data);
    int idx = gtk_combo_box_get_active(cb);
    if (idx < 0 || idx >= _n_avail) return;
    const ArchiveFmt *fmt = &FORMATS[_avail_formats[idx]];
    const char *cur = gtk_entry_get_text(entry);
    char *stripped = _strip_ext(cur);
    char *new_name = g_strconcat(stripped, fmt->ext, NULL);
    gtk_entry_set_text(entry, new_name);
    g_free(new_name);
    g_free(stripped);
}

/* Build the archiver argv. `src_rel` is a list of paths relative to `work_dir`.
 * `output` is the absolute path to the output file. */
static GPtrArray *_create_argv(const char *ext, const char *output,
                               GList *src_rel)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    gboolean use_7z = rdfm_have_7z();

    if (use_7z && (g_str_has_suffix(ext, ".7z") ||
                   g_str_has_suffix(ext, ".zip") ||
                   g_str_has_suffix(ext, ".tar.gz")  ||
                   g_str_has_suffix(ext, ".tar.xz")  ||
                   g_str_has_suffix(ext, ".tar.bz2") ||
                   g_str_has_suffix(ext, ".tar.zst") ||
                   g_str_has_suffix(ext, ".tar"))) {
        g_ptr_array_add(a, g_strdup(_7z_bin()));
        g_ptr_array_add(a, g_strdup("a"));

        /* 7z compression level: -mx=9 for 7z, -mx=5 for others */
        if (g_str_has_suffix(ext, ".7z"))
            g_ptr_array_add(a, g_strdup("-mx=9"));
        else
            g_ptr_array_add(a, g_strdup("-mx=5"));

        /* For tar variants, tell 7z to use tar format */
        if (g_str_has_suffix(ext, ".tar.gz"))
            g_ptr_array_add(a, g_strdup("-ttar"));
        /* 7z produces a plain .tar then would need pipe for .tar.gz —
         * for tar.* formats, fall through to the tar backend below */

        g_ptr_array_add(a, g_strdup(output));
        for (GList *l = src_rel; l; l = l->next)
            g_ptr_array_add(a, g_strdup(l->data));
        g_ptr_array_add(a, NULL);

        /* 7z can't natively produce .tar.gz in one step in all versions;
         * if the extension is a tar.*, override and use tar pipeline.
         * We detect this by checking if ext starts with ".tar." */
        if (g_str_has_suffix(ext, ".tar.gz")  ||
            g_str_has_suffix(ext, ".tar.bz2") ||
            g_str_has_suffix(ext, ".tar.xz")  ||
            g_str_has_suffix(ext, ".tar.zst") ||
            g_str_has_suffix(ext, ".tar")) {
            /* discard and fall through to tar backend */
            g_ptr_array_free(a, TRUE);
            a = g_ptr_array_new_with_free_func(g_free);
            use_7z = FALSE;
        } else {
            return a;
        }
    }

    /* ── tar backend ── */
    if (!use_7z || g_str_has_suffix(ext, ".tar.gz")  ||
                   g_str_has_suffix(ext, ".tar.bz2") ||
                   g_str_has_suffix(ext, ".tar.xz")  ||
                   g_str_has_suffix(ext, ".tar.zst") ||
                   g_str_has_suffix(ext, ".tar")) {
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--create"));

        if (g_str_has_suffix(ext, ".tar.gz"))       g_ptr_array_add(a, g_strdup("--gzip"));
        else if (g_str_has_suffix(ext, ".tar.bz2")) g_ptr_array_add(a, g_strdup("--bzip2"));
        else if (g_str_has_suffix(ext, ".tar.xz"))  g_ptr_array_add(a, g_strdup("--xz"));
        else if (g_str_has_suffix(ext, ".tar.zst")) g_ptr_array_add(a, g_strdup("--zstd"));

        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(output));
        for (GList *l = src_rel; l; l = l->next)
            g_ptr_array_add(a, g_strdup(l->data));
        g_ptr_array_add(a, NULL);
        return a;
    }

    /* ── zip(1) backend ── */
    if (g_str_has_suffix(ext, ".zip") && _have_zip()) {
        g_ptr_array_add(a, g_strdup("zip"));
        g_ptr_array_add(a, g_strdup("-r"));
        g_ptr_array_add(a, g_strdup(output));
        for (GList *l = src_rel; l; l = l->next)
            g_ptr_array_add(a, g_strdup(l->data));
        g_ptr_array_add(a, NULL);
        return a;
    }

    /* no tool available — return empty argv with just NULL */
    g_ptr_array_add(a, NULL);
    return a;
}

/* Additional compression options widgets (level, password, split) */
typedef struct {
    GtkWidget *level_spin;   /* compression level 1-9 */
    GtkWidget *pw_entry;     /* optional password */
    GtkWidget *pw_check;     /* enable password */
    GtkWidget *split_spin;   /* split size in MB (0 = no split) */
    GtkWidget *split_check;  /* enable split */
} ExtraOpts;

void rdfm_archive_create(GtkWindow *parent, FmPathList *files, const char *cwd)
{
    if (!rdfm_archive_enabled) return;
    if (!files || fm_path_list_get_length(files) == 0) {
        _err_dialog(parent, _("No files selected.")); return;
    }

    _build_avail();
    if (_n_avail == 0) {
        _err_dialog(parent,
            _("No archive tool found.\n"
              "Install p7zip-full (7z) or zip+tar.")); return;
    }

    /* collect absolute source paths */
    GList *abs_paths = NULL;
    for (GList *l = fm_path_list_peek_head_link(files); l; l = l->next)
        abs_paths = g_list_append(abs_paths, fm_path_to_str(l->data));

    /* common parent dir → working directory for the archiver */
    char *work_dir = _common_parent(abs_paths);

    /* build relative paths list */
    GList *rel_paths = NULL;
    for (GList *l = abs_paths; l; l = l->next)
        rel_paths = g_list_append(rel_paths, _rel_path(l->data, work_dir));
    g_list_free_full(abs_paths, g_free);

    FmPath *first    = fm_path_list_peek_head(files);
    const char *first_base = fm_path_get_basename(first);
    const char *def_ext = FORMATS[_avail_formats[0]].ext;
    char *def_name = g_strconcat(first_base, def_ext, NULL);

    /* ── dialog ── */
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Create Archive"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 440, -1);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);

    /* archive name */
    GtkWidget *name_lbl   = gtk_label_new(_("Archive name:"));
    gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0);
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(name_entry), def_name);
    gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), name_lbl,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), name_entry, FALSE, FALSE, 0);

    /* format */
    GtkWidget *fmt_lbl   = gtk_label_new(_("Format:"));
    gtk_label_set_xalign(GTK_LABEL(fmt_lbl), 0.0);
    GtkWidget *fmt_combo = gtk_combo_box_text_new();
    for (int i = 0; i < _n_avail; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(fmt_combo),
                                       FORMATS[_avail_formats[i]].label);
    gtk_combo_box_set_active(GTK_COMBO_BOX(fmt_combo), 0);
    g_signal_connect(fmt_combo, "changed",
                     G_CALLBACK(_on_fmt_changed), name_entry);
    gtk_box_pack_start(GTK_BOX(box), fmt_lbl,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), fmt_combo, FALSE, FALSE, 0);

    /* save location — defaults to cwd (where user is browsing) */
    GtkWidget *loc_lbl = gtk_label_new(_("Save in:"));
    gtk_label_set_xalign(GTK_LABEL(loc_lbl), 0.0);
    GtkWidget *loc_chooser = gtk_file_chooser_button_new(
        _("Save location"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(loc_chooser),
                                  cwd ? cwd : work_dir);
    gtk_box_pack_start(GTK_BOX(box), loc_lbl,     FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), loc_chooser, FALSE, FALSE, 0);

    /* ── extra options (7z-only section) ── */
    gtk_box_pack_start(GTK_BOX(box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    GtkWidget *extra_lbl = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(extra_lbl), _("<b>Options</b>"));
    gtk_label_set_xalign(GTK_LABEL(extra_lbl), 0.0);
    gtk_box_pack_start(GTK_BOX(box), extra_lbl, FALSE, FALSE, 0);

    /* compression level */
    GtkWidget *lvl_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lvl_lbl = gtk_label_new(_("Compression level (1–9):"));
    gtk_label_set_xalign(GTK_LABEL(lvl_lbl), 0.0);
    GtkWidget *lvl_spin = gtk_spin_button_new_with_range(1, 9, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(lvl_spin), 6);
    gtk_box_pack_start(GTK_BOX(lvl_box), lvl_lbl,  TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(lvl_box), lvl_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lvl_box, FALSE, FALSE, 0);

    /* password (7z only) */
    GtkWidget *pw_check = gtk_check_button_new_with_label(_("Password protect:"));
    GtkWidget *pw_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pw_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(pw_entry), _("password"));
    gtk_widget_set_sensitive(pw_entry, FALSE);
    g_signal_connect_swapped(pw_check, "toggled",
        G_CALLBACK(gtk_widget_set_sensitive), pw_entry);
    gtk_box_pack_start(GTK_BOX(box), pw_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), pw_entry, FALSE, FALSE, 0);

    /* split size (7z only) */
    GtkWidget *split_check = gtk_check_button_new_with_label(_("Split into volumes (MB):"));
    GtkWidget *split_spin  = gtk_spin_button_new_with_range(1, 4096, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(split_spin), 100);
    gtk_widget_set_sensitive(split_spin, FALSE);
    g_signal_connect_swapped(split_check, "toggled",
        G_CALLBACK(gtk_widget_set_sensitive), split_spin);
    GtkWidget *split_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(split_box), split_check, TRUE,  TRUE,  0);
    gtk_box_pack_start(GTK_BOX(split_box), split_spin,  FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), split_box, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_OK) {
        gtk_widget_destroy(dlg);
        g_list_free_full(rel_paths, g_free);
        g_free(work_dir); g_free(def_name);
        return;
    }

    const char *arc_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
    char *save_dir = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(loc_chooser));
    int   sel_idx  = gtk_combo_box_get_active(GTK_COMBO_BOX(fmt_combo));
    const char *sel_ext = (sel_idx >= 0 && sel_idx < _n_avail)
        ? FORMATS[_avail_formats[sel_idx]].ext : ".tar.gz";

    /* extra opts */
    int      level  = (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(lvl_spin));
    gboolean use_pw = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pw_check));
    const char *pw  = use_pw ? gtk_entry_get_text(GTK_ENTRY(pw_entry)) : NULL;
    gboolean use_split = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(split_check));
    int split_mb = use_split
        ? (int)gtk_spin_button_get_value(GTK_SPIN_BUTTON(split_spin)) : 0;

    char *output = g_build_filename(save_dir ? save_dir : cwd, arc_name, NULL);
    gtk_widget_destroy(dlg);

    GPtrArray *argv = _create_argv(sel_ext, output, rel_paths);

    /* inject level / password / split into 7z invocations */
    if (argv->len >= 2 && argv->pdata[0] &&
        (strcmp(argv->pdata[0], "7z")  == 0 ||
         strcmp(argv->pdata[0], "7za") == 0 ||
         strcmp(argv->pdata[0], "7zz") == 0))
    {
        /* remove the old -mx= arg we added in _create_argv and replace */
        for (guint i = 0; i < argv->len - 1; i++) {
            if (argv->pdata[i] && g_str_has_prefix(argv->pdata[i], "-mx=")) {
                g_free(argv->pdata[i]);
                argv->pdata[i] = g_strdup_printf("-mx=%d", level);
                break;
            }
        }
        /* insert password before the output filename (before last non-NULL arg) */
        if (pw && *pw) {
            guint ins = argv->len - 2; /* before terminating NULL */
            /* find the output path position (should be arg[2] or so) */
            g_ptr_array_insert(argv, ins, g_strconcat("-p", pw, NULL));
        }
        /* split volumes */
        if (split_mb > 0) {
            guint ins = argv->len - 2;
            g_ptr_array_insert(argv, ins,
                g_strdup_printf("-v%dm", split_mb));
        }
    }

    /* Spawn with work_dir so relative paths resolve correctly */
    GError *err = NULL;
    if (!argv->pdata[0]) {
        _err_dialog(parent,
            _("No suitable archiver found for this format."));
    } else if (!g_spawn_async(work_dir, (char **)argv->pdata, NULL,
                              G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                              NULL, NULL, NULL, &err)) {
        char *msg = g_strdup_printf(_("Failed to start archiver: %s"),
                                    err ? err->message : _("unknown error"));
        _err_dialog(parent, msg);
        g_free(msg);
        if (err) g_error_free(err);
    }

    g_ptr_array_free(argv, TRUE);
    g_list_free_full(rel_paths, g_free);
    g_free(output);
    g_free(save_dir);
    g_free(work_dir);
    g_free(def_name);
}
