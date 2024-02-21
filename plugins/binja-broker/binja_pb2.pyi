from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class BinjaInstructions(_message.Message):
    __slots__ = ("instruction",)
    class Instruction(_message.Message):
        __slots__ = ("opcode",)
        OPCODE_FIELD_NUMBER: _ClassVar[int]
        opcode: bytes
        def __init__(self, opcode: _Optional[bytes] = ...) -> None: ...
    INSTRUCTION_FIELD_NUMBER: _ClassVar[int]
    instruction: _containers.RepeatedCompositeFieldContainer[BinjaInstructions.Instruction]
    def __init__(self, instruction: _Optional[_Iterable[_Union[BinjaInstructions.Instruction, _Mapping]]] = ...) -> None: ...

class CycleCounts(_message.Message):
    __slots__ = ("cycle_count",)
    class CycleCount(_message.Message):
        __slots__ = ("ready", "executed", "is_under_pressure")
        READY_FIELD_NUMBER: _ClassVar[int]
        EXECUTED_FIELD_NUMBER: _ClassVar[int]
        IS_UNDER_PRESSURE_FIELD_NUMBER: _ClassVar[int]
        ready: int
        executed: int
        is_under_pressure: bool
        def __init__(self, ready: _Optional[int] = ..., executed: _Optional[int] = ..., is_under_pressure: bool = ...) -> None: ...
    CYCLE_COUNT_FIELD_NUMBER: _ClassVar[int]
    cycle_count: _containers.RepeatedCompositeFieldContainer[CycleCounts.CycleCount]
    def __init__(self, cycle_count: _Optional[_Iterable[_Union[CycleCounts.CycleCount, _Mapping]]] = ...) -> None: ...
