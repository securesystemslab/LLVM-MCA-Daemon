perf?=/usr/lib/linux-tools-5.15.0-130/perf  # Path to perf binary $(which perf)
# May be hard-coded like this when using a modified kernel and the corresponding
# linux-tools aren't installed (at your own risk)

.PHONY: all
all: bp_random bp_deterministic

bp_deterministic.S: gen_bp.py
	./gen_bp.py 0 > $@

bp_random.S: gen_bp.py
	./gen_bp.py 1 > $@

bp_%: bp_%.S
	gcc -no-pie -nostdlib -O0 -o $@ $<

.PHONY: eval
eval: bp_random.perf bp_deterministic.perf

%.perf: %
	$(perf) record -o $@ -F max -e cycles,branches,branch-misses,cache-misses ./$^ 

.PHONY: clean
clean:
	rm bp_deterministic bp_deterministic.S bp_random bp_random.S bp_random.perf bp_deterministic.perf
