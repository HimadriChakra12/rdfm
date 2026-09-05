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
        universal = {
            -- Tabs
            new_tab         = "<Ctrl>t",
            close_tab       = "<Ctrl>w",
            new_win         = "<Ctrl>n",
            next_tab        = "<Ctrl>Tab",
            prev_tab        = "<Ctrl><Shift>Tab",
            go_back         = "h",
            go_forward      = "l",
            go_parent       = "BackSpace",
            go_home         = "<Shift>H",
            rename          = "F2",
            copy            = "<Ctrl>c",
            cut             = "<Ctrl>x",
            paste           = "<Ctrl>v",
            delete          = "Delete",
            trash           = "<Shift>Delete",
            new_folder      = "<Ctrl><Shift>n",
            select_all      = "<Ctrl>a",
            invert_sel      = "<Ctrl>i",
            toggle_hidden   = "<Ctrl>h",
            reload          = "<Ctrl>r",
            extract_here    = "<Ctrl><Shift>e",
            compress        = "<Ctrl><Shift>c",
        },

        -- ── List view (GtkTreeView) ──────────────────────────────────────────
        list = {
            move_down       = "j",          -- next row
            move_up         = "k",          -- prev row
            move_top        = "g",          -- jump to first item
            move_bottom     = "G",          -- jump to last item
            page_down       = "<Ctrl>d",    -- half-page down
            page_up         = "<Ctrl>u",    -- half-page up
            open            = "l",          -- enter directory
            open_new_tab    = "t",
        },

        -- ── Icon / Compact / Thumbnail view (GtkIconView) ────────────────────
        icon = {
            move_down       = "j",          -- row down in grid
            move_up         = "k",          -- row up in grid
            move_left       = "h",          -- column left in grid
            move_right      = "l",          -- column right in grid
            page_down       = "<Ctrl>d",
            page_up         = "<Ctrl>u",
            open            = "Return",
            open_new_tab    = "t",
        },
    },
}
