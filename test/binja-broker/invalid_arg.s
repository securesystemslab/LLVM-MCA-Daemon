# RUN: export MCAD_PORT=50053
# RUN: llvm-mcad -mtriple=x86_64-unknown-unknown -mcpu=znver2 -load-broker-plugin=$MCAD_BIN_PATH/plugins/binja-broker/libMCADBinjaBroker.so --broker-plugin-arg-host=0.0.0.0:$MCAD_PORT | python3 %S/invalid_arg.py --port $MCAD_PORT > %t 
# RUN: FileCheck --input-file=%t %s

# CHECK:      PC Error. Code: StatusCode.INVALID_ARGUMENT
# CHECK-NEXT: Details: Errors in instruction(s) 0 (Disassembler reported Fail).
