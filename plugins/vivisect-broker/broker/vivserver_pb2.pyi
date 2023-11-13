from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class RunInstructionsRequest(_message.Message):
    __slots__ = ["numInstructions"]
    NUMINSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    numInstructions: int
    def __init__(self, numInstructions: _Optional[int] = ...) -> None: ...

class RunInstructionsReply(_message.Message):
    __slots__ = ["instructions"]
    class MemoryAccess(_message.Message):
        __slots__ = ["vAddr", "size", "isStore"]
        VADDR_FIELD_NUMBER: _ClassVar[int]
        SIZE_FIELD_NUMBER: _ClassVar[int]
        ISSTORE_FIELD_NUMBER: _ClassVar[int]
        vAddr: int
        size: int
        isStore: bool
        def __init__(self, vAddr: _Optional[int] = ..., size: _Optional[int] = ..., isStore: bool = ...) -> None: ...
    class Instruction(_message.Message):
        __slots__ = ["instruction", "opCode", "memoryAccess"]
        INSTRUCTION_FIELD_NUMBER: _ClassVar[int]
        OPCODE_FIELD_NUMBER: _ClassVar[int]
        MEMORYACCESS_FIELD_NUMBER: _ClassVar[int]
        instruction: bytes
        opCode: bytes
        memoryAccess: RunInstructionsReply.MemoryAccess
        def __init__(self, instruction: _Optional[bytes] = ..., opCode: _Optional[bytes] = ..., memoryAccess: _Optional[_Union[RunInstructionsReply.MemoryAccess, _Mapping]] = ...) -> None: ...
    INSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    instructions: _containers.RepeatedCompositeFieldContainer[RunInstructionsReply.Instruction]
    def __init__(self, instructions: _Optional[_Iterable[_Union[RunInstructionsReply.Instruction, _Mapping]]] = ...) -> None: ...
