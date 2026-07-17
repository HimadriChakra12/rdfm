# rdfm — minimal GTK3-only fork changes

## What was removed

### GTK2 support
- `--with-gtk=2` configure option removed; GTK3 is now the only target
- All `#if GTK_CHECK_VERSION(3,0,0)` / `#else` conditional branches collapsed to the GTK3 path
- `GdkColor` replaced with `GdkRGBA` throughout (`gdk_color_parse` → `gdk_rgba_parse`, `gdk_cairo_set_source_color` → `gdk_cairo_set_source_rgba`, `gtk_color_button_{get,set}_color` → `gtk_color_chooser_{get,set}_rgba`)
- GTK2-only deprecated API removed: `gdk_colormap_alloc_color`, `gdk_drawable_get_colormap`, `gdk_pixmap_new`, `gdk_cairo_create(GdkDrawable*)`, `gtk_widget_get_style`, `GtkStyle*`
- `gseal-gtk-compat.h` emptied (GTK3 has all those symbols natively)
- `configure.ac` now hardwires `gtk+-3.0` and `libfm-gtk3`

### Translations
- All 55 `.po` translation files removed (2.3 MB)
- `po/LINGUAS` generation updated to handle empty directory gracefully
- `.pot` template kept for anyone who wants to re-add translations

## What was added

### Config-driven color system (`data/rdfm.conf`)
Colors in `[desktop]` accept any CSS color string — no GTK theme dependency:
```ini
[desktop]
desktop_bg=#1a1a2e       # background fill (or any CSS color)
desktop_fg=#e0e0e0       # icon label text
desktop_shadow=#000000   # text drop shadow
```
Stored and loaded as plain `#rrggbb` hex. `GdkRGBA` used internally (0.0–1.0 range).

### Icon and GTK theme override (`[ui]` section)
```ini
[ui]
icon_theme=Papirus        # overrides system icon theme
gtk_theme=Adwaita         # overrides system GTK theme
```
Applied via `GtkSettings` at startup — works independently of `~/.config/gtk-3.0/settings.ini` or any environment variable. Commented out by default (system defaults used).

## Build
```sh
./autogen.sh
./configure
make -j$(nproc)
```
Requires: `libfm-gtk3`, `gtk+-3.0`, `glib-2.0`, `libx11`, `pango`, `intltool`
