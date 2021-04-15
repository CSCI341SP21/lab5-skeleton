SCHEDULER := lab5.c
BINARIES := lab5
CFLAGS := -g --std=c11 -Wall -Werror
LIBS := -lpthread

.PHONY: all clean burn-in test1 test2 test3

all: $(BINARIES)

lab5: main.c lab5.h $(SCHEDULER)
	$(CC) $(CFLAGS) main.c $(SCHEDULER) -o lab5 $(LIBS)

burn-in: clean lab5.c
	$(CC) $(CFLAGS) -DDELAY=1000 -DLOG_LEVEL=-1 -DPASSENGERS=100 -DTRIPS_PER_PASSENGER=100 -DELEVATORS=10 -DFLOORS=40 main.c $(SCHEDULER) -o lab5 $(LIBS)
	./lab5

test1: clean $(SCHEDULER)
	$(CC) -DDELAY=10000   -DPASSENGERS=10 -DELEVATORS=1 -DFLOORS=5 -g --std=c99 main.c $(SCHEDULER) -o lab5 $(LIBS) $(CFLAGS)

test2: clean $(SCHEDULER)
	$(CC) -DDELAY=10000   -DFLOORS=2 -g --std=c99 main.c $(SCHEDULER) -o lab5 $(LIBS) $(CFLAGS)

test3: clean $(SCHEDULER)
	$(CC) -DDELAY=10000   -DELEVATORS=10 -g --std=c99 main.c $(SCHEDULER) -o lab5 $(LIBS) $(CFLAGS)

clean:
	$(RM) -r *~ *.dSYM $(BINARIES)
