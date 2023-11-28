from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class NextAction(_message.Message):
    __slots__ = ("num_instructions",)
    NUM_INSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    num_instructions: int
    def __init__(self, num_instructions: _Optional[int] = ...) -> None: ...

class EmulatorActions(_message.Message):
    __slots__ = ("instructions",)
    class MemoryAccess(_message.Message):
        __slots__ = ("vaddr", "size", "is_store")
        VADDR_FIELD_NUMBER: _ClassVar[int]
        SIZE_FIELD_NUMBER: _ClassVar[int]
        IS_STORE_FIELD_NUMBER: _ClassVar[int]
        vaddr: int
        size: int
        is_store: bool
        def __init__(self, vaddr: _Optional[int] = ..., size: _Optional[int] = ..., is_store: bool = ...) -> None: ...
    class Instruction(_message.Message):
        __slots__ = ("opcode", "memory_access")
        OPCODE_FIELD_NUMBER: _ClassVar[int]
        MEMORY_ACCESS_FIELD_NUMBER: _ClassVar[int]
        opcode: bytes
        memory_access: EmulatorActions.MemoryAccess
        def __init__(self, opcode: _Optional[bytes] = ..., memory_access: _Optional[_Union[EmulatorActions.MemoryAccess, _Mapping]] = ...) -> None: ...
    INSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    instructions: _containers.RepeatedCompositeFieldContainer[EmulatorActions.Instruction]
    def __init__(self, instructions: _Optional[_Iterable[_Union[EmulatorActions.Instruction, _Mapping]]] = ...) -> None: ...
