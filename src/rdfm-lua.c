/*
 * rdfm-lua.c  —  Lua 5.4 configuration + keybind engine for rdfm
 *
 * Single Lua state, loaded once at startup (or reloaded explicitly).
 * Only the `rdfm` global table is consumed; everything else is sandboxed.
 *
 * Copyright (C) 2024  rdfm contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "rdfm-lua.h"

#ifdef HAVE_LUA

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <string.h>

/* ── state ───────────────────────────────────────────────────────────────── */

static lua_State *L = NULL;

/* ── helpers: safe library set ───────────────────────────────────────────── */
/*
 * We open only the pure-computation standard libs.  No io/os/package/debug
 * so a misconfigured or malicious rdfm.lua cannot touch arbitrary files.
 */
static const luaL_Reg _safe_libs[] = {
    { LUA_GNAME,      luaopen_base   },
    { LUA_TABLIBNAME, luaopen_table  },
    { LUA_STRLIBNAME, luaopen_string },
    { LUA_MATHLIBNAME,luaopen_math   },
    { NULL, NULL }
};

static void _open_safe_libs(lua_State *state)
{
    for (const luaL_Reg *lib = _safe_libs; lib->func; lib++) {
        luaL_requiref(state, lib->name, lib->func, 1);
        lua_pop(state, 1);
    }
}

/* ── helpers: path resolution ────────────────────────────────────────────── */

static char *_user_config_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "rdfm", "rdfm.lua", NULL);
}

/* Walk XDG data dirs for a system-installed rdfm.lua template. */
static char *_system_config_path(void)
{
    const gchar * const *dirs = g_get_system_data_dirs();
    for (; dirs && *dirs; dirs++) {
        char *p = g_build_filename(*dirs, "rdfm", "rdfm.lua", NULL);
        if (g_file_test(p, G_FILE_TEST_EXISTS))
            return p;
        g_free(p);
    }
    /* Fallback: path compiled into the binary by Makefile.am */
    char *p = g_build_filename(PACKAGE_DATA_DIR, "rdfm.lua", NULL);
    if (g_file_test(p, G_FILE_TEST_EXISTS))
        return p;
    g_free(p);
    return NULL;
}

/* ── lifecycle ───────────────────────────────────────────────────────────── */

gboolean rdfm_lua_load(void)
{
    lua_State *ns = luaL_newstate();
    if (!ns) {
        g_warning("rdfm-lua: failed to create Lua state (OOM?)");
        return FALSE;
    }
    _open_safe_libs(ns);

    /* Pre-seed:  rdfm = { config = {}, keybinds = {} }
     * The user script can then do  rdfm.config.key = value  or replace the
     * whole table — both work. */
    lua_newtable(ns);                                   /* {} = rdfm        */
    lua_newtable(ns); lua_setfield(ns, -2, "config");   /* rdfm.config = {} */
    lua_newtable(ns); lua_setfield(ns, -2, "keybinds"); /* rdfm.keybinds={} */
    lua_setglobal(ns, "rdfm");

    char *user_path   = _user_config_path();
    char *system_path = NULL;
    const char *load_path = NULL;

    if (g_file_test(user_path, G_FILE_TEST_EXISTS)) {
        load_path = user_path;
    } else {
        system_path = _system_config_path();
        load_path   = system_path;  /* may be NULL */
    }

    gboolean ok = TRUE;
    if (load_path) {
        if (luaL_dofile(ns, load_path) != LUA_OK) {
            g_warning("rdfm-lua: error in '%s': %s",
                      load_path, lua_tostring(ns, -1));
            lua_pop(ns, 1);
            ok = FALSE;
            /* Keep the (partially executed) state — safer than crashing. */
        } else {
            g_message("rdfm-lua: loaded '%s'", load_path);
        }
    } else {
        g_message("rdfm-lua: no rdfm.lua found; built-in defaults apply");
        ok = FALSE;  /* Callers fall back to .conf reader */
    }

    g_free(user_path);
    g_free(system_path);

    if (L) lua_close(L);
    L = ns;
    return ok;
}

gboolean rdfm_lua_loaded(void)
{
    return L != NULL;
}

void rdfm_lua_close(void)
{
    if (L) { lua_close(L); L = NULL; }
}

/* ── internal: push a config value ──────────────────────────────────────── */
/*
 * Pushes rdfm.config[key] onto the Lua stack.
 * Returns the Lua type (LUA_TSTRING, LUA_TNUMBER, …) on success,
 * or 0 (LUA_TNIL-ish) if L is NULL, the table is absent, or the key
 * is nil.  Stack is left exactly as it was on return value 0.
 */
static int _push_config_key(const char *key)
{
    if (!L) return 0;

    int base = lua_gettop(L);

    lua_getglobal(L, "rdfm");                        /* [rdfm] */
    if (!lua_istable(L, -1)) { lua_settop(L, base); return 0; }
    lua_getfield(L, -1, "config");                   /* [rdfm, config] */
    if (!lua_istable(L, -1)) { lua_settop(L, base); return 0; }
    lua_getfield(L, -1, key);                        /* [rdfm, config, val] */

    int t = lua_type(L, -1);
    if (t == LUA_TNIL) { lua_settop(L, base); return 0; }

    /* Move value below the two table refs, then pop those refs. */
    lua_insert(L, base + 1);                         /* [val, rdfm, config] */
    lua_settop(L, base + 1);                         /* [val] */
    return t;
}

/* ── rdfm.config accessors ───────────────────────────────────────────────── */

const char *rdfm_lua_config_str(const char *key, const char *def)
{
    if (!_push_config_key(key)) return def;
    /* lua_tostring() is safe to call here; the string is owned by Lua state
     * and remains valid until the next GC cycle — long enough for callers
     * that use it before yielding.  If you need it longer, g_strdup it. */
    const char *v = lua_tostring(L, -1);
    lua_pop(L, 1);
    return v ? v : def;
}

int rdfm_lua_config_int(const char *key, int def)
{
    if (!_push_config_key(key)) return def;
    int isnum = 0;
    lua_Integer v = lua_tointegerx(L, -1, &isnum);
    lua_pop(L, 1);
    return isnum ? (int)v : def;
}

gboolean rdfm_lua_config_bool(const char *key, gboolean def)
{
    if (!_push_config_key(key)) return def;
    gboolean v;
    if (lua_isboolean(L, -1))
        v = (gboolean)lua_toboolean(L, -1);
    else {
        int isnum = 0;
        lua_Integer n = lua_tointegerx(L, -1, &isnum);
        v = isnum ? (n != 0) : def;
    }
    lua_pop(L, 1);
    return v;
}

gboolean rdfm_lua_config_color(const char *key, GdkRGBA *out)
{
    if (!out) return FALSE;
    if (!_push_config_key(key)) return FALSE;
    gboolean ok = FALSE;
    if (lua_isstring(L, -1))
        ok = gdk_rgba_parse(out, lua_tostring(L, -1));
    lua_pop(L, 1);
    return ok;
}

/* ── keybind helpers ─────────────────────────────────────────────────────── */

const char *rdfm_lua_keybind_for(const char *section, const char *action)
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

    const char *accel = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;

    /* Move the string below the three table refs, pop them. */
    if (accel) {
        lua_insert(L, base + 1);
        lua_settop(L, base + 1);
        accel = lua_tostring(L, base + 1 - 1 + 1);  /* re-read after settop */
        /* Simpler: just read then clean */
    }
    /* Re-read after settling the stack cleanly */
    if (accel) {
        /* accel points into Lua's string pool — safe as long as L is live
         * and no GC runs between here and the caller using it. */
        accel = lua_tostring(L, base + 1);
    }
    lua_settop(L, base);  /* discard everything we pushed */

    /* We need a second push just to read; do it cleanly this time. */
    lua_getglobal(L, "rdfm");
    lua_getfield(L, -1, "keybinds");
    lua_remove(L, -2);
    lua_getfield(L, -1, section);
    lua_remove(L, -2);
    lua_getfield(L, -1, action);
    lua_remove(L, -2);

    const char *result = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
    /* IMPORTANT: the returned pointer is into Lua's internal string store.
     * It is valid as long as this string object is referenced somewhere in
     * the state.  Since the state lives for the entire session and we did
     * not pop it yet, this is fine — but callers must NOT g_free() it. */
    lua_pop(L, 1);
    return result;
}

/*
 * Parse a GTK accel string ("j", "<Ctrl>t", "<Shift>H") into a
 * (keyval, modmask) pair.
 */
static gboolean _parse_accel(const char *accel, guint *kv_out, int *mod_out)
{
    if (!accel || !*accel) return FALSE;

    guint kv = 0;
    GdkModifierType mods = 0;
    gtk_accelerator_parse(accel, &kv, &mods);

    if (kv == 0 && mods == 0) {
        /* Maybe it is a bare keyname like "j" or "BackSpace" */
        kv = gdk_keyval_from_name(accel);
        if (kv == GDK_KEY_VoidSymbol) return FALSE;
        mods = 0;
    }
    *kv_out  = kv;
    *mod_out = (int)mods;
    return TRUE;
}

gboolean rdfm_lua_keybind_matches(const char *section, const char *action,
                                  guint keyval, int modifier)
{
    const char *accel = rdfm_lua_keybind_for(section, action);
    if (!accel) return FALSE;

    guint ak; int am;
    if (!_parse_accel(accel, &ak, &am)) return FALSE;
    return (ak == keyval) && (am == modifier);
}

/* ── compiled-out stubs ──────────────────────────────────────────────────── */
#else /* !HAVE_LUA */

gboolean    rdfm_lua_load(void)                                         { return FALSE; }
gboolean    rdfm_lua_loaded(void)                                       { return FALSE; }
void        rdfm_lua_close(void)                                        { }
const char *rdfm_lua_config_str(const char *k, const char *d)          { (void)k; return d; }
int         rdfm_lua_config_int(const char *k, int d)                  { (void)k; return d; }
gboolean    rdfm_lua_config_bool(const char *k, gboolean d)            { (void)k; return d; }
gboolean    rdfm_lua_config_color(const char *k, GdkRGBA *o)           { (void)k; (void)o; return FALSE; }
const char *rdfm_lua_keybind_for(const char *s, const char *a)         { (void)s; (void)a; return NULL; }
gboolean    rdfm_lua_keybind_matches(const char *s, const char *a,
                                     guint kv, int m)
{
    (void)s; (void)a; (void)kv; (void)m; return FALSE;
}

#endif /* HAVE_LUA */
