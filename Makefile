#
# pua_script Makefile
#
# 
# WARNING: do not run this directly, it should be run by the master Makefile

include ../../Makefile.defs
auto_gen=
NAME=pua_script.so
LIBS=

DEFS+=-I$(SYSBASE)/include/libxml2 -I$(LOCALBASE)/include/libxml2 \
      -I$(LOCALBASE)/include
LIBS+=-L$(SYSBASE)/include/lib  -L$(LOCALBASE)/lib -lxml2

include ../../Makefile.modules
