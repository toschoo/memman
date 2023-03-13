CC = @gcc
# CC = /usr/local/musl/bin/musl-gcc -static
# CC = gcc -specs "/usr/local/musl/lib/musl-gcc.specs" -static
CXX = @g++
AR = @ar

LNKMSG = @printf "linking   $@\n"
CMPMSG = @printf "compiling $@\n"

FLGMSG = @printf "CFLAGS: $(CFLAGS)\nLDFLAGS: $(LDFLAGS)\n"

INSMSG = @printf ". setenv.sh"

#CFLAGS = -g -O2 -Wall -std=c99 -fPIC -fno-builtin -nostdlib -nostdinc -D_POSIX_C_SOURCE
#LDFLAGS = -L../xky-musl/lib -fno-builtin -nostdlib

CFLAGS = -O3 -Wall -std=c99 -fPIC -D_POSIX_C_SOURCE

# LIBCPATH=../xky-musl/lib

INC = -I.

.SUFFIXES: .c .o

.c.o:	$(DEP)
	$(CMPMSG)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

.S.o:	$(DEP)
	$(CMPMSG)
	$(CC) $(CFLAGS) $(INC) -c -o $@ $<

all:	buddysmoke ebuddysmoke ffitsmoke \
	testbuddy1 testebuddy1 testffit1 \
	montebuddy monteebuddy monteffit

buddy.o:	buddy.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -c buddy.c

ffit.o:		ffit.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -c ffit.c

buddysmoke.o:	buddysmoke.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -c buddysmoke.c

ebuddysmoke.o:	buddysmoke.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -DWITH_EMERGENCY -c buddysmoke.c -o ebuddysmoke.o

ffitsmoke.o:	buddysmoke.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -DUSEKFFIT -c buddysmoke.c -o ffitsmoke.o

buddysmoke:	buddy.o ffit.o buddysmoke.o
		$(LNKMSG)
		$(CC) -o buddysmoke buddy.o ffit.o buddysmoke.o

ebuddysmoke:	buddy.o ffit.o ebuddysmoke.o
		$(LNKMSG)
		$(CC) -o ebuddysmoke buddy.o ffit.o ebuddysmoke.o

ffitsmoke:	ffit.o ffitsmoke.o
		$(LNKMSG)
		$(CC) -o ffitsmoke ffit.o ffitsmoke.o

testffit1.o:	testbuddy1.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -DUSEKFFIT -DNOFREEPROTECT -c testbuddy1.c -o testffit1.o

testbuddy1.o:	testbuddy1.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -c testbuddy1.c -o testbuddy1.o

testebuddy1.o:	testbuddy1.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -DWITH_EMERGENCY -c testbuddy1.c -o testebuddy1.o

testbuddy1:	buddy.o testbuddy1.o ffit.o
		$(LNKMSG)
		$(CC) -o testbuddy1 buddy.o ffit.o testbuddy1.o

testebuddy1:	buddy.o testebuddy1.o ffit.o
		$(LNKMSG)
		$(CC) -o testebuddy1 buddy.o ffit.o testebuddy1.o

testffit1:	ffit.o testffit1.o
		$(LNKMSG)
		$(CC) -o testffit1 ffit.o testffit1.o

montebuddy.o:	montebuddy.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -c montebuddy.c

monteebuddy.o:	montebuddy.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -DWITH_EMERGENCY -c montebuddy.c -o monteebuddy.o

monteffit.o:	montebuddy.c
		$(CMPMSG)
		$(CC) $(CFLAGS) -g -I. -DUSEKFFIT -c montebuddy.c -o monteffit.o

montebuddy:	buddy.o ffit.o montebuddy.o
		$(LNKMSG)
		$(CC) -o montebuddy buddy.o ffit.o montebuddy.o -lm

monteebuddy:	buddy.o ffit.o monteebuddy.o
		$(LNKMSG)
		$(CC) -o monteebuddy buddy.o ffit.o monteebuddy.o -lm

monteffit:	monteffit.o ffit.o
		$(LNKMSG)
		$(CC) -o monteffit ffit.o monteffit.o -lm

clean:
	rm -f *.o
	rm -f buddysmoke
	rm -f ebuddysmoke
	rm -f ffitsmoke
	rm -f testbuddy1
	rm -f testebuddy1
	rm -f testffit1
	rm -f montebuddy
	rm -f monteebuddy
	rm -f monteffit

