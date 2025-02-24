perf?=/usr/lib/linux-tools-5.15.0-130/perf  # Path to perf binary $(which perf)
# May be hard-coded like this when using a modified kernel and the corresponding
# linux-tools aren't installed (at your own risk)

TARGETS=x

.PHONY: all
all: $(TARGETS)

x.S: gen_cache.py
	./gen_cache.py 0 > $@

.NOTINTERMEDIATE: x.S
x: x.S
	gcc -no-pie -nostdlib -O0 -o $@ $<

.PHONY: clean
clean:
	rm x
