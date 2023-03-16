from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class RunInstructionsReply(_message.Message):
    __slots__ = ["instructions"]
    class Instruction(_message.Message):
        __slots__ = ["instruction", "opCode"]
        INSTRUCTION_FIELD_NUMBER: _ClassVar[int]
        OPCODE_FIELD_NUMBER: _ClassVar[int]
        instruction: bytes
        opCode: bytes
        def __init__(self, instruction: _Optional[bytes] = ..., opCode: _Optional[bytes] = ...) -> None: ...
    INSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    instructions: _containers.RepeatedCompositeFieldContainer[RunInstructionsReply.Instruction]
    def __init__(self, instructions: _Optional[_Iterable[_Union[RunInstructionsReply.Instruction, _Mapping]]] = ...) -> None: ...

class RunInstructionsRequest(_message.Message):
    __slots__ = ["numInstructions"]
    NUMINSTRUCTIONS_FIELD_NUMBER: _ClassVar[int]
    numInstructions: int
    def __init__(self, numInstructions: _Optional[int] = ...) -> None: ...
