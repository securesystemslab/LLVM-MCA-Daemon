class WrappedInstruction:
    def __init__(self, addr=0, length=0, disasm_text=None, block=None, bytez=None, opcode=None):
        self.addr = addr
        self.length = length 
        self.block = block
        self.bytez = bytez
        self.disasm_text = disasm_text

def get_wrapped_instruction(function, addr):
    i = WrappedInstruction()
    i.addr = addr
    i.length = function.view.get_instruction_length(addr)
    i.block = function.get_basic_block_at(addr)
    i.bytez = function.view.read(addr, i.length)
    i.disasm_text = function.view.get_disassembly(addr)
    return i

class InfoCtx:
    def __init__(self, function):
        self.function = function
        self.view = function.view

        self.instructions = dict()

        self.blocks = dict()
        self.block_list = []

    def add_instruction_at_addr(self, addr):
        self.instructions[addr] = get_wrapped_instruction(self.function, addr)

    def add_block(self, block):
        if block.start in self.blocks:
            return

        self.blocks[block.start] = block
        self.block_list.append(block.start)

        for insn in block.disassembly_text:
            self.add_instruction_at_addr(insn.address)

    def get_instruction_at_addr(self, addr):
        if addr in self.instructions:
            return self.instructions[addr]
        
        return None

    def get_block_at_addr(self, addr):
        if addr in self.blocks:
            return self.blocks[addr]

        return None

def get_info_ctx_for_annotated(function):
    info_ctx = InfoCtx(function)
    view = function.view
    member_tag = view.get_tag_type("Trace Member")

    for block in function.basic_blocks:
        if any(tag.type == member_tag for tag in function.get_tags_at(block.start)):
            info_ctx.add_block(block)

    return info_ctx

def get_info_ctx_for_function(function):
    info_ctx = InfoCtx(function)

    for block in function.basic_blocks:
        info_ctx.add_block(block)

    return info_ctx
