# RUN: export MCAD_PORT=50054
# RUN: llvm-mcad -mtriple=x86_64-unknown-unknown -mcpu=znver2 -load-broker-plugin=$MCAD_BIN_PATH/plugins/binja-broker/libMCADBinjaBroker.so --broker-plugin-arg-host=0.0.0.0:$MCAD_PORT --mca-output=%t-mcad | python3 %S/sanity-x64.py --port $MCAD_PORT > %t-client
# RUN: FileCheck --input-file=%t-mcad --check-prefix=MCAD %s 
# RUN: FileCheck --input-file=%t-client --check-prefix=CLIENT %s

# MCAD:      === Printing report for Region [0] ===
# MCAD-NEXT: Iterations:        1
# MCAD-NEXT: Instructions:      3
# MCAD-NEXT: Total Cycles:      103
# MCAD-NEXT: Total uOps:        3

# MCAD:      Dispatch Width:    4
# MCAD-NEXT: uOps Per Cycle:    0.03
# MCAD-NEXT: IPC:               0.03
# MCAD-NEXT: Block RThroughput: 0.8

# CLIENT:      STATUS: success
# CLIENT-NEXT: cycle_count {  
# CLIENT-NEXT:   executed: 2  
# CLIENT-NEXT: }              
# CLIENT-NEXT: cycle_count {  
# CLIENT-NEXT:   executed: 2  
# CLIENT-NEXT: }              
# CLIENT-NEXT: cycle_count {  
# CLIENT-NEXT:   executed: 101
# CLIENT-NEXT: }              
