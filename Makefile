
CC?=gcc
CXX?=g++

CFLAGS?=  -Wall -Wfatal-errors -g -std=c++11

ARFLAGS?=rcs



SRC_DIR=.

OUTDIR?=build

BIN=$(OUTDIR)/hlsdownloader

INCLUDES+= `pkg-config --cflags libcurl`

LDFLAGS = `pkg-config --libs libcurl` -pthread

CFLAGS += $(INCLUDES)

BINOBJS= $(OUTDIR)/hlsdownloader.o 

.PHONY : all clean
all:  $(BIN)

$(BIN): $(OUTDIR)  $(BINOBJS)
	$(CXX) -o $(BIN) $(BINOBJS) $(LDFLAGS)

$(OUTDIR)/%.o :$(SRC_DIR)/%.cpp
	$(CXX) -c $(CFLAGS) -o $@ $<

$(OUTDIR):
	mkdir -p $(OUTDIR)
	
clean :
	rm -rf $(OUTDIR) 
	
