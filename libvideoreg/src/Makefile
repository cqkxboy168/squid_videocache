#
# libvideoreg.so Makefile
#

# Top level hierarchy
prefix = /usr
exec_prefix = ${prefix}
# Pathname of directory to install the include files
includedir = ${prefix}/include
# Pathname of directory to install the library
libdir =  ${exec_prefix}/lib
# Pathname of directory to install the man page
mandir = ${prefix}/man

# VPATH
srcdir = .


#
# You shouldn't need to edit anything below.
#
CC      := gcc
LD      := ld
CFLAGS  :=
LDFLAGS := -shared -fPIC
SOURCE  := videoreg.c
OBJS    := $(patsubst %.c,%.o,$(SOURCE))
TARGET_LIB := libvideoreg.so

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL}
INSTALL_DATA = ${INSTALL} -m 644

all:
	@echo Compiling $< ...
	$(CC) -c $(CFLAGS)  $(LDFLAGS) $(SOURCE) -o libvideoreg.so

.PHONY: clean

clean:
	rm *.so *.o -rf
install:
	[ -d $(DESTDIR)$(libdir) ] || \
	    (mkdir -p $(DESTDIR)$(libdir); chmod 755 $(DESTDIR)$(libdir))
	$(INSTALL_DATA) libvideoreg.so $(DESTDIR)$(libdir)/libvideoreg.so
	[ -d $(DESTDIR)$(includedir) ] || \
	    (mkdir -p $(DESTDIR)$(includedir); chmod 755 $(DESTDIR)$(includedir))
	$(INSTALL_DATA) $(srcdir)/videoreg.h $(DESTDIR)$(includedir)/videoreg.h

uninstall:
	rm -f $(DESTDIR)$(libdir)/libvideoreg.so
	rm -f $(DESTDIR)$(includedir)/videoreg.h
