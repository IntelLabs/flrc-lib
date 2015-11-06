AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS}
SUBDIRS = prt pgc tgc pillar2c toolkit

if OS_WIN32
PIL_SCRIPT_SUBDIR=windows
endif

if OS_LINUX
PIL_SCRIPT_SUBDIR=linux
endif

PIL_SCRIPT_DIR=scripts/$(PIL_SCRIPT_SUBDIR)
bin_SCRIPTS = $(PIL_SCRIPT_DIR)/pilicl $(PIL_SCRIPT_DIR)/pilink
