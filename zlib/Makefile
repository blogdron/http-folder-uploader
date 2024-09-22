################
CC=gcc
################
SRC=crc32.c    \
    trees.c    \
    zutil.c    \
    adler32.c  \
    deflate.c  \
    infback.c  \
    inffast.c  \
    inflate.c  \
    inftrees.c \
################
OBJ=crc32.o    \
    trees.o    \
    zutil.o    \
    adler32.o  \
    deflate.o  \
    infback.o  \
    inffast.o  \
    inflate.o  \
    inftrees.o \
################
all:
	$(CC)  -c $(SRC)

clean:
	-rm ./*.o

.SILENT:all clean
