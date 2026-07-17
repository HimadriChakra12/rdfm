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

defconfdir=$(sysconfdir)/xdg/rdfm/default
defconf_DATA = \
	rdfm.conf \
	$(NULL)

EXTRA_DIST= \
	$(desktop_in_files) \
	$(desktop_DATA) \
	$(defconf_DATA) \
	$(NULL)
