# RUN: llvm-mcad -mtriple=x86_64-unknown-unknown -mcpu=znver2 -load-broker-plugin=$MCAD_BIN_PATH/plugins/binja-broker/libMCADBinjaBroker.so | python3 %S/empty_arg.py > %t
# RUN: FileCheck --input-file=%t %s

# CHECK:      PC Error. Code: StatusCode.CANCELLED 
# CHECK-NEXT: Details: CANCELLED
