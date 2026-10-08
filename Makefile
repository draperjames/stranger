CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror
.PHONY: all test sanitize cross install clean
all: build/host/stranger.so build/host/test_stranger
src/metadata.h: src/module.json scripts/generate_metadata.py
	python3 scripts/generate_metadata.py
build/host:
	mkdir -p $@
build/host/stranger.so: src/stranger.c src/metadata.h src/host/audio_fx_api_v2.h src/host/plugin_api_v1.h | build/host
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden -shared -o $@ src/stranger.c -lm
build/host/test_stranger: tests/test_stranger.c src/host/audio_fx_api_v2.h src/host/plugin_api_v1.h | build/host
	$(CC) $(CFLAGS) -o $@ tests/test_stranger.c -ldl -lm
test: all
	build/host/test_stranger build/host/stranger.so
	python3 tests/test_metadata.py
sanitize: src/metadata.h
	mkdir -p build/sanitize
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fPIC -fvisibility=hidden -shared -o build/sanitize/stranger.so src/stranger.c -lm
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -o build/sanitize/test_stranger tests/test_stranger.c -ldl -lm
	build/sanitize/test_stranger build/sanitize/stranger.so
cross:
	./scripts/build.sh
install:
	./scripts/install.sh
clean:
	rm -rf build dist
