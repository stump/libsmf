include_HEADERS = smf.h

lib_LTLIBRARIES = libsmf.la
libsmf_la_SOURCES = smf.h smf_private.h smf.c smf_decode.c smf_load.c smf_save.c smf_tempo.c smf_vector.c smf_log.c smf.ver
libsmf_la_LIBADD = $(WS2_32_IF_NEEDED)
libsmf_la_LDFLAGS = -no-undefined -version-info 1:0:0 -Wl,--version-script,$(srcdir)/smf.ver

bin_PROGRAMS = smfsh
smfsh_SOURCES = smfsh.c
smfsh_LDADD = libsmf.la $(READLINE_LIBS) -lm

