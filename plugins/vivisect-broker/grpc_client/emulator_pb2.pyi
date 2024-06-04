from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, optional as _optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class NextAction(_message.Message):
    __slots__ = ["num_instructions"]
    NUM_INSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    num_instructions: int
    def __init__(self, num_instructions: _optional[int] = ...) -> None: ...

class EmulatorActions(_message.Message):
    __slots__ = ["instructions"]
    class MemoryAccess(_message.Message):
        __slots__ = ["vaddr", "size", "is_store"]
        VADDR_FIELD_NUMBER: _ClassVar[int]
        SIZE_FIELD_NUMBER: _ClassVar[int]
        IS_STORE_FIELD_NUMBER: _ClassVar[int]
        vaddr: int
        size: int
        is_store: bool
        def __init__(self, vaddr: _optional[int] = ..., size: _optional[int] = ..., is_store: bool = ...) -> None: ...
    class BranchFlow(_message.Message):
        __slots__ = ["is_mispredict"]
        IS_MISPREDICT_FIELD_NUMBER: _ClassVar[int]
        is_mispredict: bool
        def __init__(self, is_mispredict: bool = ...) -> None: ...
    class Instruction(_message.Message):
        __slots__ = ["opcode", "memory_access", "branch_flow"]
        OPCODE_FIELD_NUMBER: _ClassVar[int]
        MEMORY_ACCESS_FIELD_NUMBER: _ClassVar[int]
        BRANCH_FLOW_FIELD_NUMBER: _ClassVar[int]
        opcode: bytes
        memory_access: EmulatorActions.MemoryAccess
        branch_flow: EmulatorActions.BranchFlow
        def __init__(self, opcode: _optional[bytes] = ..., memory_access: _optional[_Union[EmulatorActions.MemoryAccess, _Mapping]] = ..., branch_flow: _optional[_Union[EmulatorActions.BranchFlow, _Mapping]] = ...) -> None: ...
    INSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    instructions: _containers.RepeatedCompositeFieldContainer[EmulatorActions.Instruction]
    def __init__(self, instructions: _optional[_Iterable[_Union[EmulatorActions.Instruction, _Mapping]]] = ...) -> None: ...
