CC ?= cc
CFLAGS = -std=c99 -Wall -Wextra -Werror -pedantic -O2
catalog-slug: main.c slug.c slug.h
	$(CC) $(CFLAGS) main.c slug.c -o $@
