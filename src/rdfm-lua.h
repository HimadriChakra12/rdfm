/*
 * rdfm-lua.h  —  Lua 5.4 configuration + keybind engine for rdfm
 *
 * Loads ~/.config/rdfm/rdfm.lua (falling back to the system-installed
 * template at $datadir/rdfm/rdfm.lua).  The file exposes one top-level
 * table:
 *
 *     rdfm = {
 *         config   = { … },   -- all options that lived in rdfm.conf
 *         keybinds = { … },   -- replaces keybinds.conf
 *     }
 *
 * keybinds is further split into subtables:
 *     universal  -- actions that work in every view
 *     list       -- list-view specific movement
 *     icon       -- icon / compact / thumbnail (all share one table)
 *
 * C consumers use the typed helpers below; they never touch the Lua state
 * directly.
 *
 * Copyright (C) 2024  rdfm contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef RDFM_LUA_H
#define RDFM_LUA_H

#include <glib.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

/* ── lifecycle ───────────────────────────────────────────────────────────── */

/*
 * rdfm_lua_load()
 *
 * Opens a fresh Lua 5.4 state, runs the user config
 * (~/.config/rdfm/rdfm.lua) and, if that is absent, the system template
 * ($XDG_DATA_DIRS/rdfm/rdfm.lua).  Safe to call more than once — reloads
 * from disk each time (useful for "Reload config").
 *
 * Returns TRUE on success; on failure emits a g_warning() and leaves the
 * previous state intact (or an empty/default state on first call).
 */
gboolean rdfm_lua_load(void);

/*
 * rdfm_lua_loaded()
 *
 * Returns TRUE if a valid Lua state is live.
 */
gboolean rdfm_lua_loaded(void);

/*
 * rdfm_lua_close()
 *
 * Tears down the Lua state.  Safe to call even when rdfm_lua_load() was
 * never called or failed.
 */
void rdfm_lua_close(void);


/* ── rdfm.config accessors ───────────────────────────────────────────────── */
/*
 * All lookups are relative to the rdfm.config table in rdfm.lua.
 * Missing keys silently return the supplied default.
 * String values are interned in a g_intern_string() pool — do NOT free them.
 */

const char *rdfm_lua_config_str  (const char *key, const char *def);
int         rdfm_lua_config_int  (const char *key, int         def);
gboolean    rdfm_lua_config_bool (const char *key, gboolean    def);

/*
 * rdfm_lua_config_color()
 *
 * Reads rdfm.config.<key> as a CSS color string and parses it into *out.
 * Returns TRUE on success, FALSE if the key is absent or unparseable.
 */
gboolean    rdfm_lua_config_color(const char *key, GdkRGBA *out);


/* ── rdfm.keybinds accessors ────────────────────────────────────────────── */

/*
 * rdfm_lua_keybind_for(section, action)
 *
 * Returns the GTK accelerator string (e.g. "j", "<Ctrl>t", "<Shift>H")
 * stored at rdfm.keybinds.<section>.<action>, or NULL if absent / disabled.
 *
 * Section names:  "universal"  |  "list"  |  "icon"
 * (icon also covers compact and thumbnail — they share one subtable)
 *
 * The returned string is owned by an internal GLib string pool —
 * do NOT free it.  It stays valid until the next rdfm_lua_load() call.
 */
const char *rdfm_lua_keybind_for(const char *section, const char *action);

/*
 * rdfm_lua_keybind_matches(section, action, keyval, modifier)
 *
 * Parses the stored accelerator and tests it against a live GdkEventKey.
 * Returns TRUE on match.
 *
 * `modifier` should already be masked with
 *     evt->state & gtk_accelerator_get_default_mod_mask()
 *
 * When Lua is not loaded (rdfm_lua_loaded() == FALSE) this always returns
 * FALSE so that the caller can fall through to its compiled-in default.
 */
gboolean rdfm_lua_keybind_matches(const char *section, const char *action,
                                  guint keyval, int modifier);

G_END_DECLS

#endif /* RDFM_LUA_H */
