CC = x86_64-w64-mingw32-gcc
CFLAGS = -Wall -O2 -shared

test.dll: test_dll.c
	$(CC) $(CFLAGS) $< -o $@ -luser32

.PHONY: clean
clean:
	rm -f test.dll
