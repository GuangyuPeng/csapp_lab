# Makefile for the CS:APP Shell Lab

TEAM = NOBODY
VERSION = 1
HANDINDIR = /afs/cs/academic/class/15213-f02/L5/handin
BINDIR = ./bin
TRACEDIR = ./traces
DRIVER = ./util/sdriver.pl
TSH = $(BINDIR)/tsh
TSHREF = $(BINDIR)/tshref
TSHARGS = "-p"
CC = gcc
CFLAGS = -Wall -O2
FILES = tsh myspin mysplit mystop myint

all: $(FILES)

$(FILES): %: %.c
	$(CC) $(CFLAGS) $< -o $(BINDIR)/$@

##################
# Handin your work
##################
handin:
	cp tsh.c $(HANDINDIR)/$(TEAM)-$(VERSION)-tsh.c


##################
# Regression tests
##################

# Run tests using the student's shell program
test01:
	$(DRIVER) -t $(TRACEDIR)/trace01.txt -s $(TSH) -a $(TSHARGS)
test02:
	$(DRIVER) -t $(TRACEDIR)/trace02.txt -s $(TSH) -a $(TSHARGS)
test03:
	$(DRIVER) -t $(TRACEDIR)/trace03.txt -s $(TSH) -a $(TSHARGS)
test04:
	$(DRIVER) -t $(TRACEDIR)/trace04.txt -s $(TSH) -a $(TSHARGS)
test05:
	$(DRIVER) -t $(TRACEDIR)/trace05.txt -s $(TSH) -a $(TSHARGS)
test06:
	$(DRIVER) -t $(TRACEDIR)/trace06.txt -s $(TSH) -a $(TSHARGS)
test07:
	$(DRIVER) -t $(TRACEDIR)/trace07.txt -s $(TSH) -a $(TSHARGS)
test08:
	$(DRIVER) -t $(TRACEDIR)/trace08.txt -s $(TSH) -a $(TSHARGS)
test09:
	$(DRIVER) -t $(TRACEDIR)/trace09.txt -s $(TSH) -a $(TSHARGS)
test10:
	$(DRIVER) -t $(TRACEDIR)/trace10.txt -s $(TSH) -a $(TSHARGS)
test11:
	$(DRIVER) -t $(TRACEDIR)/trace11.txt -s $(TSH) -a $(TSHARGS)
test12:
	$(DRIVER) -t $(TRACEDIR)/trace12.txt -s $(TSH) -a $(TSHARGS)
test13:
	$(DRIVER) -t $(TRACEDIR)/trace13.txt -s $(TSH) -a $(TSHARGS)
test14:
	$(DRIVER) -t $(TRACEDIR)/trace14.txt -s $(TSH) -a $(TSHARGS)
test15:
	$(DRIVER) -t $(TRACEDIR)/trace15.txt -s $(TSH) -a $(TSHARGS)
test16:
	$(DRIVER) -t $(TRACEDIR)/trace16.txt -s $(TSH) -a $(TSHARGS)

# Run the tests using the reference shell program
rtest01:
	$(DRIVER) -t $(TRACEDIR)/trace01.txt -s $(TSHREF) -a $(TSHARGS)
rtest02:
	$(DRIVER) -t $(TRACEDIR)/trace02.txt -s $(TSHREF) -a $(TSHARGS)
rtest03:
	$(DRIVER) -t $(TRACEDIR)/trace03.txt -s $(TSHREF) -a $(TSHARGS)
rtest04:
	$(DRIVER) -t $(TRACEDIR)/trace04.txt -s $(TSHREF) -a $(TSHARGS)
rtest05:
	$(DRIVER) -t $(TRACEDIR)/trace05.txt -s $(TSHREF) -a $(TSHARGS)
rtest06:
	$(DRIVER) -t $(TRACEDIR)/trace06.txt -s $(TSHREF) -a $(TSHARGS)
rtest07:
	$(DRIVER) -t $(TRACEDIR)/trace07.txt -s $(TSHREF) -a $(TSHARGS)
rtest08:
	$(DRIVER) -t $(TRACEDIR)/trace08.txt -s $(TSHREF) -a $(TSHARGS)
rtest09:
	$(DRIVER) -t $(TRACEDIR)/trace09.txt -s $(TSHREF) -a $(TSHARGS)
rtest10:
	$(DRIVER) -t $(TRACEDIR)/trace10.txt -s $(TSHREF) -a $(TSHARGS)
rtest11:
	$(DRIVER) -t $(TRACEDIR)/trace11.txt -s $(TSHREF) -a $(TSHARGS)
rtest12:
	$(DRIVER) -t $(TRACEDIR)/trace12.txt -s $(TSHREF) -a $(TSHARGS)
rtest13:
	$(DRIVER) -t $(TRACEDIR)/trace13.txt -s $(TSHREF) -a $(TSHARGS)
rtest14:
	$(DRIVER) -t $(TRACEDIR)/trace14.txt -s $(TSHREF) -a $(TSHARGS)
rtest15:
	$(DRIVER) -t $(TRACEDIR)/trace15.txt -s $(TSHREF) -a $(TSHARGS)
rtest16:
	$(DRIVER) -t $(TRACEDIR)/trace16.txt -s $(TSHREF) -a $(TSHARGS)


# clean up
BINS = $(FILES:%=$(BINDIR)/%)
clean:
	rm -f $(BINS) $(BINDIR)/*.o $(BINDIR)/*~


