/*
 * rdfm-archive.c  —  Archive support for rdfm
 *
 * Supports: tar, tar.gz, tar.bz2, tar.xz, tar.zst, zip, 7z, rar, gz, bz2, xz
 * via CLI tools. No libarchive dependency; graceful fallback when optional
 * tools (7z, rar) are absent.
 *
 * Features:
 *   1. View contents (file list dialog)
 *   2. Password dialog for encrypted archives
 *   3. Extract with destination chooser (defaults to <name>/ subfolder)
 *   4. Create archive with format picker
 */

#include "rdfm-archive.h"

#include <string.h>
#include <errno.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libfm/fm.h>
#include <libfm/fm-gtk.h>

/* ── tool detection ──────────────────────────────────────────────────────── */

gboolean rdfm_have_7z(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (g_find_program_in_path("7z") != NULL  ||
                  g_find_program_in_path("7za") != NULL ||
                  g_find_program_in_path("7zz") != NULL) ? 1 : 0;
    return (gboolean)cached;
}

gboolean rdfm_have_rar(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = (g_find_program_in_path("unrar") != NULL ||
                  g_find_program_in_path("rar") != NULL) ? 1 : 0;
    return (gboolean)cached;
}

static const char *_7z_bin(void)
{
    static char *bin = NULL;
    if (!bin) {
        if (g_find_program_in_path("7z"))   bin = "7z";
        else if (g_find_program_in_path("7za"))  bin = "7za";
        else if (g_find_program_in_path("7zz"))  bin = "7zz";
        else bin = "7z";
    }
    return bin;
}

static const char *_unrar_bin(void)
{
    static char *bin = NULL;
    if (!bin) {
        if (g_find_program_in_path("unrar")) bin = "unrar";
        else if (g_find_program_in_path("rar")) bin = "rar";
        else bin = "unrar";
    }
    return bin;
}

static const char *_rar_create_bin(void)
{
    static char *bin = NULL;
    if (!bin) {
        if (g_find_program_in_path("rar")) bin = "rar";
        else bin = _7z_bin(); /* fallback to 7z */
    }
    return bin;
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
    return rdfm_archive_detect(filename) != RDFM_ARCHIVE_UNKNOWN;
}

/* ── small helpers ───────────────────────────────────────────────────────── */

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

/* Strip trailing archive extensions to get a base name */
static char *_strip_ext(const char *basename)
{
    static const char *exts[] = {
        ".tar.gz",".tar.bz2",".tar.xz",".tar.zst",".tar.lz4",
        ".tgz",".tbz2",".tbz",".txz",".tzst",
        ".tar",".zip",".7z",".rar",".gz",".bz2",".xz",".zst", NULL
    };
    for (int i = 0; exts[i]; i++)
        if (g_str_has_suffix(basename, exts[i]))
            return g_strndup(basename, strlen(basename) - strlen(exts[i]));
    return g_strdup(basename);
}

/* ── list command ────────────────────────────────────────────────────────── */

static GPtrArray *_list_argv(RdfmArchiveType t, const char *path,
                             const char *pw)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
    switch (t) {
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
    case RDFM_ARCHIVE_ZIP:
        g_ptr_array_add(a, g_strdup("unzip"));
        g_ptr_array_add(a, g_strdup("-Z1"));
        if (pw) {
            g_ptr_array_add(a, g_strdup("-P"));
            g_ptr_array_add(a, g_strdup(pw));
        }
        g_ptr_array_add(a, g_strdup(path));
        break;
    case RDFM_ARCHIVE_7Z:
    case RDFM_ARCHIVE_GZ:
    case RDFM_ARCHIVE_BZ2:
    case RDFM_ARCHIVE_XZ:
    case RDFM_ARCHIVE_ZSTD:
        g_ptr_array_add(a, g_strdup(_7z_bin()));
        g_ptr_array_add(a, g_strdup("l"));
        g_ptr_array_add(a, g_strdup("-ba")); /* bare: no header/footer */
        if (pw) g_ptr_array_add(a, g_strconcat("-p", pw, NULL));
        g_ptr_array_add(a, g_strdup(path));
        break;
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

static GList *_parse_listing(RdfmArchiveType t, const char *output)
{
    if (!output || !*output) return NULL;
    GList *list = NULL;
    char **lines = g_strsplit(output, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *line = g_strstrip(lines[i]);
        if (!*line) continue;

        if (t == RDFM_ARCHIVE_7Z || t == RDFM_ARCHIVE_GZ ||
            t == RDFM_ARCHIVE_BZ2 || t == RDFM_ARCHIVE_XZ ||
            t == RDFM_ARCHIVE_ZSTD) {
            /* 7z -ba format: "Date Time Attr  Size Comp  Name"
             * Fields separated by 2+ spaces; name is last. */
            gchar **f = g_regex_split_simple(" {2,}", line, 0, 0);
            int n = g_strv_length(f);
            if (n >= 2 && f[n-1] && *f[n-1])
                list = g_list_prepend(list, g_strdup(f[n-1]));
            g_strfreev(f);
        } else {
            list = g_list_prepend(list, g_strdup(line));
        }
    }
    g_strfreev(lines);
    return g_list_reverse(list);
}

/* ── view dialog ─────────────────────────────────────────────────────────── */

enum { COL_ICON = 0, COL_NAME, COL_N };

/* filter model visible func: show row when name contains search text */
static gboolean _row_visible(GtkTreeModel *model, GtkTreeIter *iter,
                             gpointer data)
{
    GtkEntry *entry = GTK_ENTRY(data);
    const char *text = gtk_entry_get_text(entry);
    if (!text || !*text) return TRUE;
    char *name = NULL;
    gtk_tree_model_get(model, iter, COL_NAME, &name, -1);
    gboolean vis = (name != NULL) && (strcasestr(name, text) != NULL);
    g_free(name);
    return vis;
}

static void _on_search_changed(GtkSearchEntry *se, gpointer filter)
{
    (void)se;
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(filter));
}

/* Passed to the "Extract" button response handler */
typedef struct {
    GtkWindow *parent;
    char      *filepath;
} ViewExtractData;

static void _view_extract_data_free(gpointer p)
{
    ViewExtractData *d = p;
    g_free(d->filepath);
    g_free(d);
}

void rdfm_archive_view(GtkWindow *parent, const char *filepath)
{
    const char *basename = g_path_get_basename(filepath);
    RdfmArchiveType type = rdfm_archive_detect(basename);

    if (type == RDFM_ARCHIVE_UNKNOWN) {
        _err_dialog(parent, _("Unsupported archive format.")); return;
    }
    if (type == RDFM_ARCHIVE_7Z && !rdfm_have_7z()) {
        _err_dialog(parent, _("Install p7zip to open .7z archives.")); return;
    }
    if (type == RDFM_ARCHIVE_RAR && !rdfm_have_rar()) {
        _err_dialog(parent, _("Install unrar to open .rar archives.")); return;
    }

    /* Try listing; ask for password if encrypted */
    char *password = NULL;
    GList *entries = NULL;

retry:;
    GPtrArray *argv = _list_argv(type, filepath, password);
    int xst = 0;
    char *out = _run_capture((const char **)argv->pdata, NULL, &xst);
    g_ptr_array_free(argv, TRUE);

    gboolean need_pw =
        (xst != 0 && !password) &&
        (!out || strstr(out, "password") || strstr(out, "encrypted") ||
         strstr(out, "Wrong password"));

    if (need_pw) {
        g_free(out);
        password = _ask_password(parent, basename);
        if (!password) return;
        goto retry;
    }

    entries = _parse_listing(type, out);
    g_free(out);

    if (!entries) {
        _err_dialog(parent, _("Could not read archive contents."));
        g_free(password);
        return;
    }

    /* build list store */
    GtkListStore *store = gtk_list_store_new(COL_N,
                              GDK_TYPE_PIXBUF, G_TYPE_STRING);
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    for (GList *l = entries; l; l = l->next) {
        const char *name = l->data;
        const char *icon_name =
            g_str_has_suffix(name, "/")     ? "folder"          :
            g_str_has_suffix(name, ".png")  ||
            g_str_has_suffix(name, ".jpg")  ||
            g_str_has_suffix(name, ".jpeg") ||
            g_str_has_suffix(name, ".svg")  ? "image-x-generic" :
                                               "text-x-generic";
        GdkPixbuf *pb = gtk_icon_theme_load_icon(theme, icon_name, 16,
                             GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, COL_ICON, pb, COL_NAME, name, -1);
        if (pb) g_object_unref(pb);
    }

    /* filter model + search */
    GtkWidget *search = gtk_search_entry_new();
    gtk_widget_set_tooltip_text(search, _("Filter entries"));

    GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER(
        gtk_tree_model_filter_new(GTK_TREE_MODEL(store), NULL));
    g_object_unref(store);

    gtk_tree_model_filter_set_visible_func(filter, _row_visible,
                                           search, NULL);
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

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        basename, parent, GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Extract…", GTK_RESPONSE_APPLY,
        "_Close",    GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 560, 460);

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
    char *path = fm_path_to_str(fm_file_info_get_path(fi));
    rdfm_archive_view(parent, path);
    g_free(path);
}

/* ── extract ─────────────────────────────────────────────────────────────── */

static GPtrArray *_extract_argv(RdfmArchiveType t, const char *path,
                                const char *dest, const char *pw)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);
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
    case RDFM_ARCHIVE_7Z:
    case RDFM_ARCHIVE_GZ:
    case RDFM_ARCHIVE_BZ2:
    case RDFM_ARCHIVE_XZ:
    case RDFM_ARCHIVE_ZSTD:
        g_ptr_array_add(a, g_strdup(_7z_bin()));
        g_ptr_array_add(a, g_strdup("x"));
        g_ptr_array_add(a, g_strdup("-y"));
        if (pw) g_ptr_array_add(a, g_strconcat("-p", pw, NULL));
        g_ptr_array_add(a, g_strdup(path));
        g_ptr_array_add(a, g_strconcat("-o", dest, NULL));
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
    default:
        break;
    }
    g_ptr_array_add(a, NULL);
    return a;
}

void rdfm_archive_extract(GtkWindow *parent, const char *filepath,
                          const char *dest_dir_hint)
{
    const char *basename = g_path_get_basename(filepath);
    RdfmArchiveType type = rdfm_archive_detect(basename);

    if (type == RDFM_ARCHIVE_UNKNOWN) {
        _err_dialog(parent, _("Unsupported archive format.")); return;
    }
    if (type == RDFM_ARCHIVE_7Z && !rdfm_have_7z()) {
        _err_dialog(parent, _("Install p7zip to extract .7z archives.")); return;
    }
    if (type == RDFM_ARCHIVE_RAR && !rdfm_have_rar()) {
        _err_dialog(parent, _("Install unrar to extract .rar archives.")); return;
    }

    char *archive_dir  = g_path_get_dirname(filepath);
    char *base_noext   = _strip_ext(basename);
    char *default_dest = dest_dir_hint
        ? g_strdup(dest_dir_hint)
        : g_build_filename(archive_dir, base_noext, NULL);

    /* ── destination dialog ── */
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Extract Archive"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel",  GTK_RESPONSE_CANCEL,
        "_Extract", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 460, -1);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);

    char *markup = g_markup_printf_escaped(
        "<b>%s</b>\n<small>%s</small>",
        _("Extract to:"), basename);
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title), markup);
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    g_free(markup);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    /* folder chooser */
    GtkWidget *chooser = gtk_file_chooser_button_new(
        _("Choose Destination"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(chooser),
        g_file_test(default_dest, G_FILE_TEST_IS_DIR)
            ? default_dest : archive_dir);
    gtk_box_pack_start(GTK_BOX(box), chooser, FALSE, FALSE, 0);

    /* subfolder option */
    GtkWidget *sub_check = gtk_check_button_new_with_label(
        _("Extract into subfolder:"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sub_check), TRUE);
    GtkWidget *sub_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(sub_entry), base_noext);
    gtk_entry_set_activates_default(GTK_ENTRY(sub_entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), sub_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sub_entry, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_OK) {
        gtk_widget_destroy(dlg);
        g_free(default_dest); g_free(base_noext); g_free(archive_dir);
        return;
    }

    char *chosen = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    gboolean use_sub = gtk_toggle_button_get_active(
                           GTK_TOGGLE_BUTTON(sub_check));
    const char *sub = gtk_entry_get_text(GTK_ENTRY(sub_entry));
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
        g_free(default_dest); g_free(base_noext); g_free(archive_dir);
        return;
    }

    char *password = NULL;
do_extract:;
    GPtrArray *argv = _extract_argv(type, filepath, dest, password);
    int xst = 0;
    char *out = _run_capture((const char **)argv->pdata, NULL, &xst);
    g_ptr_array_free(argv, TRUE);

    gboolean need_pw = (xst != 0) && !password &&
        (out == NULL || strstr(out, "password") ||
         strstr(out, "encrypted") || strstr(out, "Wrong password"));
    g_free(out);

    if (need_pw) {
        password = _ask_password(parent, basename);
        if (password) goto do_extract;
    } else if (xst != 0) {
        _err_dialog(parent,
            _("Extraction failed. The archive may be corrupted or "
              "require a password."));
    }
    /* on success: nothing extra — the file manager will refresh */

    g_free(password);
    g_free(dest);
    g_free(default_dest);
    g_free(base_noext);
    g_free(archive_dir);
}

void rdfm_archive_extract_files(GtkWindow *parent, FmFileInfoList *files)
{
    for (GList *l = fm_file_info_list_peek_head_link(files); l; l = l->next) {
        FmFileInfo *fi = l->data;
        if (!rdfm_is_archive(fm_file_info_get_name(fi))) continue;
        char *path = fm_path_to_str(fm_file_info_get_path(fi));
        rdfm_archive_extract(parent, path, NULL);
        g_free(path);
    }
}

/* ── create ──────────────────────────────────────────────────────────────── */

typedef struct {
    const char *label;
    const char *ext;
    gboolean    need_7z;
    gboolean    need_rar;
} ArchiveFmt;

static const ArchiveFmt FORMATS[] = {
    { "tar.gz  — gzip compressed",    ".tar.gz",  FALSE, FALSE },
    { "tar.xz  — xz compressed",      ".tar.xz",  FALSE, FALSE },
    { "tar.bz2 — bzip2 compressed",   ".tar.bz2", FALSE, FALSE },
    { "tar.zst — zstd compressed",    ".tar.zst", FALSE, FALSE },
    { "tar     — uncompressed",       ".tar",     FALSE, FALSE },
    { "zip",                          ".zip",     FALSE, FALSE },
    { "7z",                           ".7z",      TRUE,  FALSE },
    { "rar",                          ".rar",     FALSE, TRUE  },
    { NULL, NULL, FALSE, FALSE }
};

/* Indices into FORMATS that are currently available */
static int _avail_formats[G_N_ELEMENTS(FORMATS)];
static int _n_avail = 0;

static void _build_avail(void)
{
    _n_avail = 0;
    for (int i = 0; FORMATS[i].label; i++) {
        if (FORMATS[i].need_7z  && !rdfm_have_7z())  continue;
        if (FORMATS[i].need_rar && !rdfm_have_rar()) continue;
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
    char *stripped = _strip_ext(g_path_get_basename(cur));
    char *new_name = g_strconcat(stripped, fmt->ext, NULL);
    gtk_entry_set_text(entry, new_name);
    g_free(new_name);
    g_free(stripped);
}

static GPtrArray *_create_argv(const char *ext, const char *output,
                               GList *src_paths)
{
    GPtrArray *a = g_ptr_array_new_with_free_func(g_free);

    if (g_str_has_suffix(ext, ".tar.gz"))  {
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--create"));
        g_ptr_array_add(a, g_strdup("--gzip"));
        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(output));
    } else if (g_str_has_suffix(ext, ".tar.xz")) {
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--create"));
        g_ptr_array_add(a, g_strdup("--xz"));
        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(output));
    } else if (g_str_has_suffix(ext, ".tar.bz2")) {
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--create"));
        g_ptr_array_add(a, g_strdup("--bzip2"));
        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(output));
    } else if (g_str_has_suffix(ext, ".tar.zst")) {
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--create"));
        g_ptr_array_add(a, g_strdup("--zstd"));
        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(output));
    } else if (g_str_has_suffix(ext, ".tar")) {
        g_ptr_array_add(a, g_strdup("tar"));
        g_ptr_array_add(a, g_strdup("--create"));
        g_ptr_array_add(a, g_strdup("--file"));
        g_ptr_array_add(a, g_strdup(output));
    } else if (g_str_has_suffix(ext, ".zip")) {
        g_ptr_array_add(a, g_strdup("zip"));
        g_ptr_array_add(a, g_strdup("-r"));
        g_ptr_array_add(a, g_strdup(output));
    } else if (g_str_has_suffix(ext, ".7z")) {
        g_ptr_array_add(a, g_strdup(_7z_bin()));
        g_ptr_array_add(a, g_strdup("a"));
        g_ptr_array_add(a, g_strdup(output));
    } else if (g_str_has_suffix(ext, ".rar")) {
        g_ptr_array_add(a, g_strdup(_rar_create_bin()));
        g_ptr_array_add(a, g_strdup("a"));
        g_ptr_array_add(a, g_strdup(output));
    }

    for (GList *l = src_paths; l; l = l->next)
        g_ptr_array_add(a, g_strdup(l->data));
    g_ptr_array_add(a, NULL);
    return a;
}

void rdfm_archive_create(GtkWindow *parent, FmPathList *files, const char *cwd)
{
    if (!files || fm_path_list_get_length(files) == 0) {
        _err_dialog(parent, _("No files selected.")); return;
    }

    _build_avail();

    FmPath *first    = fm_path_list_peek_head(files);
    char   *def_name = g_strconcat(fm_path_get_basename(first), ".tar.gz", NULL);

    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Create Archive"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create", GTK_RESPONSE_OK, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 420, -1);

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_box_set_spacing(GTK_BOX(box), 8);

    GtkWidget *name_lbl   = gtk_label_new(_("Archive name:"));
    gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.0);
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(name_entry), def_name);
    gtk_entry_set_activates_default(GTK_ENTRY(name_entry), TRUE);
    gtk_box_pack_start(GTK_BOX(box), name_lbl,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), name_entry, FALSE, FALSE, 0);

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

    GtkWidget *loc_lbl    = gtk_label_new(_("Save in:"));
    gtk_label_set_xalign(GTK_LABEL(loc_lbl), 0.0);
    GtkWidget *loc_chooser = gtk_file_chooser_button_new(
        _("Save location"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
    if (cwd) gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(loc_chooser), cwd);
    gtk_box_pack_start(GTK_BOX(box), loc_lbl,    FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), loc_chooser, FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    if (gtk_dialog_run(GTK_DIALOG(dlg)) != GTK_RESPONSE_OK) {
        gtk_widget_destroy(dlg);
        g_free(def_name);
        return;
    }

    const char *arc_name = gtk_entry_get_text(GTK_ENTRY(name_entry));
    char *save_dir   = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(loc_chooser));
    int   sel_idx    = gtk_combo_box_get_active(GTK_COMBO_BOX(fmt_combo));
    const char *sel_ext = (sel_idx >= 0 && sel_idx < _n_avail)
        ? FORMATS[_avail_formats[sel_idx]].ext : ".tar.gz";
    char *output = g_build_filename(save_dir ? save_dir : cwd, arc_name, NULL);

    gtk_widget_destroy(dlg);

    /* collect source paths */
    GList *src = NULL;
    for (GList *l = fm_path_list_peek_head_link(files); l; l = l->next)
        src = g_list_append(src, fm_path_to_str(l->data));

    GPtrArray *argv = _create_argv(sel_ext, output, src);

    GError *err = NULL;
    if (!g_spawn_async(cwd, (char **)argv->pdata, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                       NULL, NULL, NULL, &err)) {
        char *msg = g_strdup_printf(_("Failed to start archiver: %s"),
                                    err ? err->message : _("unknown error"));
        _err_dialog(parent, msg);
        g_free(msg);
        if (err) g_error_free(err);
    }

    g_ptr_array_free(argv, TRUE);
    g_list_free_full(src, g_free);
    g_free(output);
    g_free(save_dir);
    g_free(def_name);
}
