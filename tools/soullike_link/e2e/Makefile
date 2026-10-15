# Builds the headless mGBA harness used by run_e2e.py.
# MGBA_DIR must point at an mGBA source tree built by build_mgba.sh.
MGBA_DIR ?= ../../../mgba

harness: harness.c
	$(CC) -O2 -o $@ $< -I$(MGBA_DIR)/include -I$(MGBA_DIR)/build/include \
		-DM_CORE_GBA -DENABLE_SCRIPTING -DUSE_LUA \
		$(MGBA_DIR)/build/libmgba.a -llua5.4 -lz -lpng -lm -lpthread

clean:
	rm -f harness

.PHONY: clean
