-- rdfm.lua — unified configuration for rdfm file manager
-- Location: ~/.config/rdfm/rdfm.lua
--
-- This single file replaces both rdfm.conf and keybinds.conf.
-- All values here are the defaults; uncomment and edit to customise.
--
-- Keybind format
-- ──────────────
--   Plain key    : "j"          (lowercase letter, no modifier)
--   Shifted key  : "<Shift>H"   or just "H" when unambiguous
--   With Ctrl    : "<Ctrl>t"
--   With Alt     : "<Alt>Left"
--   Function key : "F5"
--   Set to false to disable a binding entirely.
--
-- Colors accept any CSS/X11 color string: "#rrggbb", "rgb()", named colors.

rdfm = {

    -- ── General / behaviour ──────────────────────────────────────────────
    config = {

        -- How bookmarks are opened: 0 = current tab, 1 = new tab, 2 = new window
        bm_open_method     = 0,

        -- Volume management
        mount_on_startup   = true,
        mount_removable    = true,
        autorun            = true,

        -- Desktop integration
        show_wm_menu       = false,

        -- ── Window ──────────────────────────────────────────────────────
        win_width          = 900,
        win_height         = 560,
        maximized          = false,
        splitter_pos       = 200,       -- side-pane width in pixels

        -- ── Tabs ────────────────────────────────────────────────────────
        always_show_tabs   = false,
        hide_close_btn     = false,
        max_tab_chars      = 32,
        media_in_new_tab   = false,
        desktop_folder_new_win = false,
        change_tab_on_drop = false,
        close_on_unmount   = true,
        focus_previous     = false,     -- focus previous tab on close

        -- ── Side pane ───────────────────────────────────────────────────
        -- "places"  │ "dirtree"  │ "hidden"  │ "places,hidden" (comma-sep list)
        side_pane_mode     = "places",

        -- ── View ────────────────────────────────────────────────────────
        -- "icon" │ "list" │ "compact" │ "thumbnail"
        view_mode          = "icon",
        show_hidden        = false,

        -- Sort: "<column>;<direction>;"   column: name/size/mtime/type
        --                                 direction: ascending/descending
        sort               = "name;ascending;",

        -- ── Toolbar ─────────────────────────────────────────────────────
        -- comma-separated list of items to show:
        --   "visible"    – show the toolbar at all
        --   "newwin"     – New Window button
        --   "newtab"     – New Tab button
        --   "navigation" – Back/Forward/Up buttons
        --   "home"       – Home button
        toolbar            = "visible,navigation,home",

        -- ── Status bar ──────────────────────────────────────────────────
        show_statusbar     = true,
        pathbar_mode_buttons = false,

        -- ── Theme overrides ──────────────────────────────────────────────
        -- Uncomment to force a specific theme regardless of GTK settings:
        -- icon_theme  = "Papirus",
        -- gtk_theme   = "Adwaita",

        -- Font for the UI (Pango description string). nil = system default.
        app_font           = "JetBrains Mono 11",

        -- ── Colors ───────────────────────────────────────────────────────
        -- nil / absent = inherit from GTK theme
        app_bg             = "#1a1a2e",
        app_fg             = "#e0e0e0",
        toolbar_bg         = "#16213e",
        toolbar_fg         = "#e0e0e0",
        pathbar_bg         = "#0f3460",
        pathbar_fg         = "#ffffff",
        view_bg            = "#1a1a2e",
        view_fg            = "#e0e0e0",
        sel_bg             = "#e040fb",
        sel_fg             = "#1a1a2e",
        side_pane_bg       = "#16213e",
        side_pane_fg       = "#e0e0e0",
    },

    -- ── Keybinds ─────────────────────────────────────────────────────────
    keybinds = {

        -- ── Universal (work in every view and context) ──────────────────
        universal = {
            -- Tabs
            new_tab         = "<Ctrl>t",
            close_tab       = "<Ctrl>w",
            new_win         = "<Ctrl>n",
            next_tab        = "<Ctrl>Tab",
            prev_tab        = "<Ctrl><Shift>Tab",

            -- Navigation
            go_back         = "h",
            go_forward      = "l",
            go_parent       = "BackSpace",
            go_home         = "<Shift>H",

            -- File operations
            rename          = "F2",
            copy            = "<Ctrl>c",
            cut             = "<Ctrl>x",
            paste           = "<Ctrl>v",
            delete          = "Delete",
            trash           = "<Shift>Delete",
            new_folder      = "<Ctrl><Shift>n",
            select_all      = "<Ctrl>a",
            invert_sel      = "<Ctrl>i",

            -- View toggles
            toggle_hidden   = "<Ctrl>h",
            reload          = "<Ctrl>r",

            -- Path bar / search
            focus_path      = "/",

            -- Archive
            extract_here    = "<Ctrl><Shift>e",
            compress        = "<Ctrl><Shift>c",
        },

        -- ── List view ────────────────────────────────────────────────────
        -- (These already work via GTK's tree-view; they are here so you can
        --  remap them.  rdfm's list keybinds are already 10/10 — see the
        --  existing implementation.  Overrides are applied on top.)
        list = {
            move_down       = "j",
            move_up         = "k",
            move_top        = "g",          -- gg handled in view handler
            move_bottom     = "G",
            page_down       = "<Ctrl>d",
            page_up         = "<Ctrl>u",
            open            = "Return",
            open_new_tab    = "t",
            expand_dir      = "l",          -- expand tree node (list-tree mode)
            collapse_dir    = "h",
        },

        -- ── Icon / Compact / Thumbnail view ──────────────────────────────
        -- All three visual modes share these bindings.
        icon = {
            move_down       = "j",
            move_up         = "k",
            move_left       = "h",
            move_right      = "l",
            page_down       = "<Ctrl>d",
            page_up         = "<Ctrl>u",
            open            = "Return",
            open_new_tab    = "t",
        },
    },
}
