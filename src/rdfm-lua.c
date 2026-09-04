/*
 * rdfm-lua.c — Lua 5.4 config engine for rdfm
 *
 * Loads ~/.config/rdfm/rdfm.lua. The file must define a global table:
 *
 *   rdfm = {
 *       config   = { ... },
 *       keybinds = { ... },
 *   }
 *
 * Or split across files using include()/require():
 *
 *   rdfm = { config = include("config"), keybinds = include("keybinds") }
 *
 * All string accessors return g_strdup'd copies — caller must g_free().
 * Bool/int accessors return by value with a compiled-in default fallback.
 *
 * Copyright (C) 2024 rdfm contributors — GPL-2.0-or-later
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "rdfm-lua.h"

#ifdef HAVE_LUA

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <glib.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <string.h>

/* ── state ─────────────────────────────────────────────────────────────── */

static lua_State *L        = NULL;
static char      *_cfg_dir = NULL;  /* dir of the loaded config file */

/* ── safe libs (no io/os/package — sandboxed) ───────────────────────────── */

static const luaL_Reg _safe_libs[] = {
    { LUA_GNAME,       luaopen_base   },
    { LUA_TABLIBNAME,  luaopen_table  },
    { LUA_STRLIBNAME,  luaopen_string },
    { LUA_MATHLIBNAME, luaopen_math   },
    { NULL, NULL }
};

/* ── include()/require() ────────────────────────────────────────────────── */

static int _lua_include(lua_State *ls)
{
    const char *name = luaL_checkstring(ls, 1);
    if (!_cfg_dir) {
        lua_pushstring(ls, "include(): no config dir set");
        return lua_error(ls);
    }

    char *path = g_str_has_suffix(name, ".lua")
        ? g_build_filename(_cfg_dir, name, NULL)
        : g_build_filename(_cfg_dir, g_strconcat(name, ".lua", NULL), NULL);

    /* security: must stay inside config dir */
    char *real     = realpath(path, NULL);
    char *real_dir = realpath(_cfg_dir, NULL);
    g_free(path);
    gboolean safe = real && real_dir && g_str_has_prefix(real, real_dir);
    free(real_dir);
    if (!safe) {
        free(real);
        lua_pushfstring(ls, "include(): '%s' outside config dir", name);
        return lua_error(ls);
    }

    if (luaL_loadfile(ls, real) != LUA_OK) { free(real); return lua_error(ls); }
    free(real);
    lua_call(ls, 0, LUA_MULTRET);
    return lua_gettop(ls) - 1;
}

/* ── lifecycle ──────────────────────────────────────────────────────────── */

gboolean rdfm_lua_load(void)
{
    lua_State *ns = luaL_newstate();
    if (!ns) { g_warning("rdfm-lua: OOM"); return FALSE; }

    for (const luaL_Reg *lib = _safe_libs; lib->func; lib++) {
        luaL_requiref(ns, lib->name, lib->func, 1);
        lua_pop(ns, 1);
    }

    /* pre-seed rdfm = { config={}, keybinds={} } */
    lua_newtable(ns);
    lua_newtable(ns); lua_setfield(ns, -2, "config");
    lua_newtable(ns); lua_setfield(ns, -2, "keybinds");
    lua_setglobal(ns, "rdfm");

    /* resolve config path */
    char *user = g_build_filename(g_get_user_config_dir(), "rdfm", "rdfm.lua", NULL);
    char *path = NULL;

    if (g_file_test(user, G_FILE_TEST_EXISTS)) {
        path = user; user = NULL;
    } else {
        const gchar * const *dirs = g_get_system_data_dirs();
        for (; dirs && *dirs; dirs++) {
            path = g_build_filename(*dirs, "rdfm", "rdfm.lua", NULL);
            if (g_file_test(path, G_FILE_TEST_EXISTS)) break;
            g_free(path); path = NULL;
        }
        if (!path) {
            char *p = g_build_filename(PACKAGE_DATA_DIR, "rdfm.lua", NULL);
            if (g_file_test(p, G_FILE_TEST_EXISTS)) path = p; else g_free(p);
        }
        g_free(user);
    }

    gboolean ok = FALSE;
    if (path) {
        g_free(_cfg_dir);
        _cfg_dir = g_path_get_dirname(path);

        lua_pushcfunction(ns, _lua_include);
        lua_setglobal(ns, "include");
        lua_pushcfunction(ns, _lua_include);
        lua_setglobal(ns, "require");

        if (luaL_dofile(ns, path) == LUA_OK) {
            g_message("rdfm-lua: loaded '%s'", path);
            ok = TRUE;
        } else {
            g_warning("rdfm-lua: '%s': %s", path, lua_tostring(ns, -1));
            lua_pop(ns, 1);
        }
        g_free(path);
        g_free(_cfg_dir); _cfg_dir = NULL;
    } else {
        g_message("rdfm-lua: no rdfm.lua found");
    }

    if (L) lua_close(L);
    L = ns;
    return ok;
}

gboolean rdfm_lua_loaded(void) { return L != NULL; }

void rdfm_lua_close(void)
{
    if (L) { lua_close(L); L = NULL; }
}

/* ── internal: get rdfm.config[key] as a Lua type, leave on stack ───────── */

static int _push_key(const char *key)
{
    if (!L) return LUA_TNIL;
    int base = lua_gettop(L);
    lua_getglobal(L, "rdfm");
    if (!lua_istable(L, -1)) { lua_settop(L, base); return LUA_TNIL; }
    lua_getfield(L, -1, "config");
    if (!lua_istable(L, -1)) { lua_settop(L, base); return LUA_TNIL; }
    lua_getfield(L, -1, key);
    int t = lua_type(L, -1);
    if (t == LUA_TNIL) { lua_settop(L, base); return LUA_TNIL; }
    lua_insert(L, base + 1);
    lua_settop(L, base + 1);
    return t;
}

/* ── config accessors — all strings are g_strdup'd, caller must g_free() ── */

char *rdfm_lua_config_str(const char *key, const char *def)
{
    if (_push_key(key) == LUA_TNIL)
        return def ? g_strdup(def) : NULL;
    const char *v = lua_tostring(L, -1);
    char *out = v ? g_strdup(v) : (def ? g_strdup(def) : NULL);
    lua_pop(L, 1);
    return out;
}

int rdfm_lua_config_int(const char *key, int def)
{
    if (_push_key(key) == LUA_TNIL) return def;
    int isnum = 0;
    int v = (int)lua_tointegerx(L, -1, &isnum);
    lua_pop(L, 1);
    return isnum ? v : def;
}

gboolean rdfm_lua_config_bool(const char *key, gboolean def)
{
    int t = _push_key(key);
    if (t == LUA_TNIL) return def;
    gboolean v = lua_isboolean(L, -1) ? (gboolean)lua_toboolean(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

gboolean rdfm_lua_config_color(const char *key, GdkRGBA *out)
{
    if (!out || _push_key(key) == LUA_TNIL) return FALSE;
    gboolean ok = lua_isstring(L, -1) && gdk_rgba_parse(out, lua_tostring(L, -1));
    lua_pop(L, 1);
    return ok;
}

/* ── keybind accessors ──────────────────────────────────────────────────── */

/* Returns g_strdup'd accel string or NULL — caller must g_free() */
char *rdfm_lua_keybind_for(const char *section, const char *action)
{
    if (!L) return NULL;
    int base = lua_gettop(L);
    lua_getglobal(L, "rdfm");
    if (!lua_istable(L, -1)) { lua_settop(L, base); return NULL; }
    lua_getfield(L, -1, "keybinds");
    if (!lua_istable(L, -1)) { lua_settop(L, base); return NULL; }
    lua_getfield(L, -1, section);
    if (!lua_istable(L, -1)) { lua_settop(L, base); return NULL; }
    lua_getfield(L, -1, action);
    char *out = lua_isstring(L, -1) ? g_strdup(lua_tostring(L, -1)) : NULL;
    lua_settop(L, base);
    return out;
}

gboolean rdfm_lua_keybind_matches(const char *section, const char *action,
                                   guint keyval, int modifier)
{
    char *accel = rdfm_lua_keybind_for(section, action);
    if (!accel) return FALSE;
    guint kv = 0; GdkModifierType mods = 0;
    gtk_accelerator_parse(accel, &kv, &mods);
    if (kv == 0) kv = gdk_keyval_from_name(accel);
    g_free(accel);
    return kv && kv != GDK_KEY_VoidSymbol &&
           kv == keyval && (guint)mods == (guint)modifier;
}

#else /* !HAVE_LUA */

gboolean  rdfm_lua_load(void)                                        { return FALSE; }
gboolean  rdfm_lua_loaded(void)                                      { return FALSE; }
void      rdfm_lua_close(void)                                       { }
char     *rdfm_lua_config_str(const char *k, const char *d)         { (void)k; return d ? g_strdup(d) : NULL; }
int       rdfm_lua_config_int(const char *k, int d)                 { (void)k; return d; }
gboolean  rdfm_lua_config_bool(const char *k, gboolean d)           { (void)k; return d; }
gboolean  rdfm_lua_config_color(const char *k, GdkRGBA *o)         { (void)k; (void)o; return FALSE; }
char     *rdfm_lua_keybind_for(const char *s, const char *a)        { (void)s; (void)a; return NULL; }
gboolean  rdfm_lua_keybind_matches(const char *s, const char *a,
                                    guint kv, int m)
{
    (void)s; (void)a; (void)kv; (void)m; return FALSE;
}

#endif /* HAVE_LUA */
