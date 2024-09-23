###################################
CC=gcc
AR=ar
###################################
ENABLE_PAGE    =1
ENABLE_ZLIB    =1
ENABLE_SUNZIP  =1
ENABLE_ZIPFLOW =1
ENABLE_LIBC    =1
ENABLE_LOG     =0
ENABLE_FORK    =1
###################################
APP=server
SRC=server.c
LIB=libzip.a
###################################
OBJ_LIBS+= $(ZLIB_DIR)/*.o
OBJ_LIBS+= $(SUNZIP_DIR)/*.o
OBJ_LIBS+= $(ZIPFLOW_DIR)/*.o
###################################
ifeq ($(ENABLE_ZLIB),1)
CFLAGS+= -DENABLE_ZLIB
endif
#####
ifeq ($(ENABLE_SUNZIP),1)
CFLAGS+= -DENABLE_SUNZIP
endif
#####
ifeq ($(ENABLE_ZIPFLOW),1)
CFLAGS+= -DENABLE_ZIPFLOW
endif
#####
ifeq ($(ENABLE_LIBC),1)
CFLAGS+= -DENABLE_LIBC
endif
#####
ifeq ($(ENABLE_FORK),1)
CFLAGS+= -DENABLE_FORK
endif
#####
ifeq ($(ENABLE_LOG),1)
CFLAGS+= -DENABLE_LOG
endif
#####
ifeq ($(ENABLE_PAGE),1)
CFLAGS+= -DENABLE_PAGE
endif
LFLAGS=-L. -lzip
###################################
PAGE_DIR:=page
ZLIB_DIR:=zlib
SUNZIP_DIR:=sunzip
ZIPFLOW_DIR:=zipflow
###################################

all:
	$(MAKE) -C $(PAGE_DIR) all
	$(MAKE) -C $(ZLIB_DIR)  all
	$(MAKE) -C $(SUNZIP_DIR) all
	$(MAKE) -C $(ZIPFLOW_DIR) all
	$(AR)  rcs $(LIB) $(OBJ_LIBS)
	$(CC) $(SRC) $(CFLAGS) $(LFLAGS) -o $(APP)

clean:
	$(MAKE) -C $(PAGE_DIR) clean
	$(MAKE) -C $(ZLIB_DIR)  clean
	$(MAKE) -C $(SUNZIP_DIR) clean
	$(MAKE) -C $(ZIPFLOW_DIR) clean
	-rm $(APP)
	-rm $(LIB)
	-rm -rd jail

test:all
	mkdir -p  jail
	cp $(APP) jail
	touch jail/itworks.txt
	firejail --private=jail ./$(APP) $(PWD)

.SILENT: clean
