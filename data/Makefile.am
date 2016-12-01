SUBDIRS = icons

mimedir = $(datadir)/mime/packages
mime_DATA = sysprof-mime.xml

desktopdir = $(datadir)/applications
desktop_DATA = org.gnome.Sysprof2.desktop

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = sysprof-$(API_VERSION).pc
if ENABLE_GTK
pkgconfig_DATA += sysprof-ui-$(API_VERSION).pc
endif

gsettings_SCHEMAS = org.gnome.sysprof2.gschema.xml

.PRECIOUS: $(gsettings_SCHEMAS)

@GSETTINGS_RULES@

EXTRA_DIST = \
	sysprof.pc.in \
	org.gnome.Sysprof2.desktop \
	$(mime_DATA) \
	$(gsettings_SCHEMAS) \
	meson.build \
	$(NULL)

DISTCLEANFILES = $(pkgconfig_DATA)
GITIGNOREFILES = $(dbusservice_DATA)


if ENABLE_SYSPROFD

dbusservicedir = $(datadir)/dbus-1/system-services
dbusservice_in_files = org.gnome.Sysprof2.service.in
dbusservice_DATA = $(dbusservice_in_files:.service.in=.service)

$(dbusservice_DATA): $(dbusservice_in_files) Makefile
	@sed -e "s|\@sysprofdprivdir\@|$(libexecdir)/$(PACKAGE)|" $< > $@

dbusconfdir = $(datadir)/dbus-1/system.d
dbusconf_in_files = org.gnome.Sysprof2.conf.in
dbusconf_DATA = $(dbusconf_in_files:.conf.in=.conf)

systemdservice_in_files = sysprof2.service.in
systemdservicedir = $(systemdsystemunitdir)
systemdservice_DATA = $(systemdservice_in_files:.service.in=.service)

$(systemdservice_DATA): $(systemdservice_in_files) Makefile
	@sed -e "s|\@sysprofdprivdir\@|$(libexecdir)/$(PACKAGE)|" $< > $@

polkitdir = $(datadir)/polkit-1/actions
polkit_in_files = org.gnome.sysprof2.policy.in
polkit_DATA = $(polkit_in_files:.policy.in=.policy)

DISTCLEANFILES += \
	$(systemdservice_DATA) \
	$(dbusservice_DATA) \
	$(NULL)

EXTRA_DIST += \
	org.gnome.Sysprof2.xml \
	$(dbusservice_in_files) \
	$(dbusconf_in_files) \
	$(systemdservice_in_files) \
	$(polkit_in_files) \
	$(NULL)

GITIGNOREFILES += $(systemdservice_DATA)

endif


install-data-local: install-mimeDATA
	if [ -f $(DESTDIR)$(datadir)/mime/packages/freedesktop.org.xml ] ; then \
	  if which update-mime-database>/dev/null 2>&1; then \
	    update-mime-database $(DESTDIR)$(datadir)/mime; \
	  fi \
	fi

uninstall-local:
	if [ -f $(DESTDIR)$(datadir)/mime/packages/freedesktop.org.xml ] ; then \
	  if which update-mime-database>/dev/null 2>&1; then \
	    update-mime-database $(DESTDIR)$(datadir)/mime; \
	  fi \
	fi


-include $(top_srcdir)/git.mk
