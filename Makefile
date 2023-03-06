SRCS = src/main.c src/gpt.c src/fat32.c

efimaker: $(SRCS) Makefile
	$(CC) -ggdb -O2 -o $@ -Wall -Werror $(SRCS) -lz
