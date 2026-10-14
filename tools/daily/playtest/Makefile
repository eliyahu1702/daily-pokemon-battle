# Headless playtest driver. Needs libmgba-dev and libpng-dev (Ubuntu: apt install libmgba-dev libpng-dev).
playtest: playtest.c
	$(CC) -O2 -o $@ $< -lmgba -lpng

clean:
	rm -f playtest
