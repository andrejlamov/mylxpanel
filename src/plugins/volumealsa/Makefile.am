if BUILD_ALSA_PLUGINS

volumealsa_la_CFLAGS = \
	-I. \
	-I$(top_srcdir)/src \
	-DPACKAGE_DATA_DIR=\""$(datadir)/lxpanel"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = volumealsa.la

moduledir = $(libdir)/lxpanel/plugins

volumealsa_la_SOURCES = volumealsa.c

volumealsa_la_LIBADD = -lasound $(PACKAGE_LIBS)

volumealsa_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@

endif
