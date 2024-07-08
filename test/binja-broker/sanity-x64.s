# RUN: llvm-mcad -mtriple=x86_64-unknown-unknown -mcpu=znver2 -load-broker-plugin=$MCAD_BIN_PATH/plugins/binja-broker/libMCADBinjaBroker.so --mca-output=%t | python3 %S/sanity-x64.py
# RUN: FileCheck --input-file=%t %s

# CHECK:      === Printing report for Region [0] ===
# CHECK-NEXT: Iterations:        1
# CHECK-NEXT: Instructions:      3
# CHECK-NEXT: Total Cycles:      103
# CHECK-NEXT: Total uOps:        3

# CHECK:      Dispatch Width:    4
# CHECK-NEXT: uOps Per Cycle:    0.03
# CHECK-NEXT: IPC:               0.03
# CHECK-NEXT: Block RThroughput: 0.8
