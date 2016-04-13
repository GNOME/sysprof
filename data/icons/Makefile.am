icondir = $(datadir)/icons/hicolor

nobase_icon_DATA = \
	16x16/apps/sysprof.png \
	24x24/apps/sysprof.png \
	32x32/apps/sysprof.png \
	48x48/apps/sysprof.png \
	256x256/apps/sysprof.png \
	scalable/apps/sysprof-symbolic.svg \
	$(NULL)

EXTRA_DIST = \
	$(nobase_icon_DATA) \
	sysprof-source.svg

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi

-include $(top_srcdir)/git.mk
