CC ?= gcc
CFLAGS += -Wall -Wextra -O2
CFLAGS += -I../ta/include
LDLIBS += -lteec

all: hesia_ca

hesia_ca: main.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	$(RM) *.o hesia_ca
