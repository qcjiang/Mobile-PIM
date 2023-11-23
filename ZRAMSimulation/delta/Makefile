CC=g++
CFLAGS=
LZO_SRC=lzo/minilzo.c
LZ4_SRC=lz4/lz4.c

all:  EvalReadCompressedFile


EvalReadCompressedFile: EvalReadCompressedFile.cpp
		$(CC) $(CFLAGS) EvalReadCompressedFile.cpp $(LZO_SRC) $(LZ4_SRC) -o EvalReadCompressedFile

clean:
		rm -f EvalWriteCompressedFile EvalReadCompressedFile
