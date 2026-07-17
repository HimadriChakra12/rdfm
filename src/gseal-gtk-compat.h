/* GTK3-only build: no GTK2 compatibility shims needed.
 * GDK_KEY_* and gtk_widget_get_mapped/realized/etc. are all
 * standard in GTK >= 3.0. This file is kept as an empty guard
 * so existing #include directives do not need to be removed. */

#ifndef GSEAL_GTK_COMPAT_H
#define GSEAL_GTK_COMPAT_H

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION(2, 28, 0)
#define g_list_free_full(list, free_func) \
{ \
g_list_foreach(list, (GFunc)free_func, NULL); \
g_list_free(list); \
}
#endif

G_END_DECLS

#endif /* GSEAL_GTK_COMPAT_H */
