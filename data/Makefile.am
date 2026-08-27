NULL=

SUBDIRS=ui

man_MANS=rdfm.1

desktopdir=$(datadir)/applications
desktop_in_files= \
	rdfm.desktop.in \
	rdfm-desktop-pref.desktop.in \
	$(NULL)
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

# Install rdfm.lua as the system-default config template.
# Users copy/symlink this to ~/.config/rdfm/rdfm.lua and edit from there.
# rdfm.conf is kept for backward compatibility.
defconfdir=$(sysconfdir)/xdg/rdfm/default
defconf_DATA = \
	rdfm.conf \
	$(NULL)

pkgdatadir=$(datadir)/rdfm
pkgdata_DATA = \
	rdfm.lua \
	$(NULL)

EXTRA_DIST= \
	$(desktop_in_files) \
	$(desktop_DATA) \
	$(defconf_DATA) \
	rdfm.lua \
	$(NULL)
