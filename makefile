# main
EXE=main

# Main target
all: $(EXE)

CFLG=-g -std=c++11
LIBS=-pthread -lcrypto
CLEAN=rm proxy.o; rm proxy

# Compile
.cpp.o:
	g++ -c $(CFLG) $<  $(LIBS)
# Link
main: proxy.o
	g++ -o proxy proxy.o  $(LIBS)

#  Clean
clean:
	$(CLEAN)
