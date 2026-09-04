/*
 * rdfm-lua.h — Lua 5.4 config engine for rdfm
 *
 * All rdfm_lua_config_str() and rdfm_lua_keybind_for() return g_strdup'd
 * strings. Caller must g_free() them.
 *
 * Copyright (C) 2024 rdfm contributors — GPL-2.0-or-later
 */
#ifndef RDFM_LUA_H
#define RDFM_LUA_H

#include <glib.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

/* lifecycle */
gboolean  rdfm_lua_load(void);
gboolean  rdfm_lua_loaded(void);
void      rdfm_lua_close(void);

/* config — strings are g_strdup'd, g_free() after use */
char     *rdfm_lua_config_str  (const char *key, const char *def);
int       rdfm_lua_config_int  (const char *key, int         def);
gboolean  rdfm_lua_config_bool (const char *key, gboolean    def);
gboolean  rdfm_lua_config_color(const char *key, GdkRGBA    *out);

/* keybinds — string is g_strdup'd, g_free() after use */
char     *rdfm_lua_keybind_for    (const char *section, const char *action);
gboolean  rdfm_lua_keybind_matches(const char *section, const char *action,
                                    guint keyval, int modifier);

G_END_DECLS
#endif /* RDFM_LUA_H */
