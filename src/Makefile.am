bin_PROGRAMS = mymc

shared_srcdir = $(HOME)

INCLUDES =  -I$(shared_srcdir)/include -I/usr/include -I/usr/local/include -DPREFIX=\"${prefix}\"  -D_GNU_SOURCE 


mymc_SOURCES = main.c \
		ini.c \
		config.c \
		init.c \
		mc.c \
		io_buffer.c 
		

mymc_LDFLAGS = -L/usr/lib -L/usr/local/lib -pthread -ltokyocabinet -lz -lbz2 -lresolv  -ldl  -lpthread -lm -lc -lev 
mymc_LDADD = -L$(shared_srcdir)/lib 

install-conf: install
	echo -------------------------------------------------
	echo $(DESTDIR)
	if [ -n "${prefix}" ]; then \
		if ! test -d ${prefix}/conf; then mkdir ${prefix}/conf; fi; \
		cp config.ini ${prefix}/conf/config.ini; \
		if ! test -d ${prefix}/data; then mkdir ${prefix}/data; fi; \
	fi 
