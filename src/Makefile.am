NULL=

AM_CPPFLAGS = \
	-DPACKAGE_DATA_DIR=\""$(datadir)/rdfm"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/rdfm/ui"\" \
	-DPACKAGE_MODULES_DIR=\""$(libdir)/rdfm"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

bin_PROGRAMS = rdfm

rdfm_SOURCES = \
	rdfm.c \
	app-config.c \
	main-win.c \
	tab-page.c \
	desktop.c \
	volume-manager.c \
	pref.c \
	single-inst.c \
	connect-server.c \
	$(NULL)

EXTRA_DIST= \
	main-win-ui.c \
	desktop-ui.c \
	rdfm.h \
	app-config.h \
	main-win.h \
	tab-page.h \
	desktop.h \
	volume-manager.h \
	pref.h \
	single-inst.h \
	connect-server.h \
	gseal-gtk-compat.h \
	$(NULL)

include_HEADERS = rdfm-modules.h

rdfm_CFLAGS = \
	$(XLIB_CFLAGS) \
	$(FM_CFLAGS) \
	$(G_CAST_CHECKS) \
	-Wall \
	-Werror-implicit-function-declaration \
	$(NULL)

rdfm_LDADD = \
	$(XLIB_LIBS) \
	$(FM_LIBS) \
	$(NULL)

# prepare modules directory
install-exec-local:
	$(MKDIR_P) "$(DESTDIR)$(libdir)/rdfm"

uninstall-local:
	-rmdir "$(DESTDIR)$(libdir)/rdfm"
