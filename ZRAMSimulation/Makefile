CC=g++
CFLAGS=
LZO_SRC=lzo/minilzo.c
LZ4_SRC=lz4/lz4.c

all: EvalWriteCompressedFile EvalReadCompressedFile

EvalWriteCompressedFile: EvalWriteCompressedFile.cpp
		$(CC) $(CFLAGS) EvalWriteCompressedFile.cpp $(LZO_SRC) $(LZ4_SRC) -o EvalWriteCompressedFile

EvalReadCompressedFile: EvalReadCompressedFile.cpp
		$(CC) $(CFLAGS) EvalReadCompressedFile.cpp $(LZO_SRC) $(LZ4_SRC) -o EvalReadCompressedFile

clean:
		rm -f EvalWriteCompressedFile EvalReadCompressedFile
