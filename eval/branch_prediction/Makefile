perf?=/usr/lib/linux-tools-5.15.0-130/perf  # Path to perf binary $(which perf)
# May be hard-coded like this when using a modified kernel and the corresponding
# linux-tools aren't installed (at your own risk)

BP_TARGETS=bp_0 bp_1 bp_2 bp_3 bp_4 bp_5 bp_6 bp_7 bp_8

.PHONY: all
all: $(BP_TARGETS)

bp_%.S: gen_bp.py
	./gen_bp.py $* > $@

.NOTINTERMEDIATE: $(BP_TARGETS:%=%.S)
bp_%: bp_%.S
	gcc -no-pie -nostdlib -O0 -o $@ $<

.PHONY: eval
eval: $(BP_TARGETS:%=eval_%.txt)

eval_%.txt: %
	$(perf) stat -e cycles,branches,branch-misses ./$^ 2>&1 | tee $@

.PHONY: clean
clean:
	rm bp_* eval_*.txt
