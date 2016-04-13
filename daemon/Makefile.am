if ENABLE_SYSPROFD

pkglibexec_PROGRAMS = sysprofd

sysprofd_SOURCES = \
	sysprofd.c \
	sd-bus-helper.c \
	sd-bus-helper.h

sysprofd_LDADD = $(SYSTEMD_LIBS)

endif

-include $(top_srcdir)/git.mk
