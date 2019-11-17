// Automatically generate string opcodes

var core = require("./autogen");
var slice_section = core.slice_section
var get_section = core.get_section;
var parseEnum = core.parseEnum;

function prettify(str){
    var lines = str.split("\n"), res = [];
    var indent = 0;
    for(var i=0;i<lines.length;i++){
        var line = lines[i].trim();
        if(!line) continue;
        //console.log(line, indent);
        var tempIndent = 0;
        if(line[0] === "#") {
            tempIndent = indent;
            indent = 0;
        }
        if(line.indexOf("}") !== -1) indent--;
        for(var j=0;j<indent;j++) line = "    " + line;
        if(line.indexOf("{") !== -1) indent++;
        res.push(line);
        if(line[0] === "#") 
            indent = tempIndent;
    }
    return res.join("\n") + "\n";
}

function m(f) {
    var str = f.toString().
    replace(/^[^\/]+\/\*!?/, '').
    replace(/\*\/[^\/]+$/, '');
    var i = 1;
    for (; i < arguments.length; i++) {
        str = str.replace(new RegExp("\\$" + (i - 1), "g"), arguments[i]);
    }
    return prettify(str + "\n");
}

var size_endings = ["????????????? ", "b", "w", "???????", "d"];
var funcs = [];

var templates = {
    "movs": function (osize, asize) {
        var add = "-" + osize + " : " + osize,
            szspc = osize << 3;
        var regspec = asize === 16 ? "16[" : "32[E";
        funcs.push("movs" + size_endings[osize]+ asize);
        return m(function () {
            /*
int movs$4$3(int flags)
{
    int count = cpu.reg$2CX], add = cpu.eflags & EFLAGS_DF ? $1, src, ds_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
        count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read$0(ds_base + cpu.reg$2SI], src, cpu.tlb_shift_read);
        cpu_write$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_write);
        cpu.reg$2SI] += add;
        cpu.reg$2DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read$0(ds_base + cpu.reg$2SI], src, cpu.tlb_shift_read);
        cpu_write$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_write);
        cpu.reg$2SI] += add;
        cpu.reg$2DI] += add;
        cpu.reg$2CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg$2CX] != 0;
}
            */
        }, szspc, add, regspec, asize, size_endings[osize]);
    },
    "stos": function (osize, asize) {
        var add = "-" + osize + " : " + osize,
            szspc = osize << 3;
        var regspec = asize === 16 ? "16[" : "32[E";
        var al;
        switch (osize) {
            case 1:
                al = "cpu.reg8[AL]";
                break;
            case 2:
                al = "cpu.reg16[AX]";
                break;
            case 4:
                al = "cpu.reg32[EAX]";
                break;
        }
        funcs.push("stos" + size_endings[osize]+ asize);
        return m(function () {
            /*
int stos$5$3(int flags)
{
    int count = cpu.reg$2CX], add = cpu.eflags & EFLAGS_DF ? $1, src = $4;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
        count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_write$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_write);
        cpu.reg$2DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_write$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_write);
        cpu.reg$2DI] += add;
        cpu.reg$2CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg$2CX] != 0;
}
            */
        }, szspc, add, regspec, asize, al, size_endings[osize]);
    },
    "scas": function (osize, asize) {
        var add = "-" + osize + " : " + osize,
            szspc = osize << 3;
        var regspec = asize === 16 ? "16[" : "32[E";
        var al;
        switch (osize) {
            case 1:
                al = "cpu.reg8[AL]";
                break;
            case 2:
                al = "cpu.reg16[AX]";
                break;
            case 4:
                al = "cpu.reg32[EAX]";
                break;
        }
        funcs.push("scas" + size_endings[osize]+ asize);
        return m(function () {
            /*
int scas$5$3(int flags)
{
    int count = cpu.reg$2CX], add = cpu.eflags & EFLAGS_DF ? $1;
    uint$0_t dest = $4, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
        count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
            cpu_read$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_read);
            cpu.reg$2DI] += add;
            cpu.lr = (int$0_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB$0;
            return 0;
        case 1: // REPZ
            for (int i = 0; i < count; i++) {
                cpu_read$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_read);
                cpu.reg$2DI] += add;
                cpu.reg$2CX]--;

                // XXX don't set this every time
                cpu.lr = (int$0_t)(dest - src);
                cpu.lop2 = src;
                cpu.laux = SUB$0;

                //cpu.cycles_to_run--;
                if(src != dest) return 0;
            }
            return cpu.reg$2CX] != 0;
        case 2: // REPNZ
            for (int i = 0; i < count; i++) {
                cpu_read$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_read);
                cpu.reg$2DI] += add;
                cpu.reg$2CX]--;
                
                // XXX don't set this every time
                cpu.lr = (int$0_t)(dest - src);
                cpu.lop2 = src;
                cpu.laux = SUB$0;

                //cpu.cycles_to_run--;
                if(src == dest) return 0;
            }
            return cpu.reg$2CX] != 0;
    }
    CPU_FATAL("unreachable");
}
            */
        }, szspc, add, regspec, asize, al, size_endings[osize]);
    },
    "ins": function (osize, asize) {
        var add = "-" + osize + " : " + osize,
            szspc = osize << 3;
        var regspec = asize === 16 ? "16[" : "32[E";
        var al;
        switch (osize) {
            case 1:
                al = "cpu.reg8[AL]";
                break;
            case 2:
                al = "cpu.reg16[AX]";
                break;
            case 4:
                al = "cpu.reg32[EAX]";
                break;
        }
        funcs.push("ins" + size_endings[osize]+ asize);
        return m(function () {
            /*
int ins$4$3(int flags)
{
    int count = cpu.reg$2CX], add = cpu.eflags & EFLAGS_DF ? $1, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
        count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], $0 >> 3)) return -1;

    if(!repz_or_repnz(flags)){
        src = cpu_in$4(cpu.reg16[DX]);
        cpu_write$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_write);
        cpu.reg$2DI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        src = cpu_in$4(cpu.reg16[DX]);
        cpu_write$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_write);
        cpu.reg$2DI] += add;
        cpu.reg$2CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg$2CX] != 0;
}
            */
        }, szspc, add, regspec, asize, size_endings[osize]);
    },
    "outs": function (osize, asize) {
        var add = "-" + osize + " : " + osize,
            szspc = osize << 3;
        var regspec = asize === 16 ? "16[" : "32[E";
        var al;
        switch (osize) {
            case 1:
                al = "cpu.reg8[AL]";
                break;
            case 2:
                al = "cpu.reg16[AX]";
                break;
            case 4:
                al = "cpu.reg32[EAX]";
                break;
        }
        funcs.push("outs" + size_endings[osize]+ asize);
        return m(function () {
            /*
int outs$4$3(int flags)
{
    int count = cpu.reg$2CX], add = cpu.eflags & EFLAGS_DF ? $1, src, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
        count = MAX_CYCLES_TO_RUN;
    if(cpu_io_check_access(cpu.reg16[DX], $0 >> 3)) return -1;

    if(!repz_or_repnz(flags)){
        cpu_read$0(seg_base + cpu.reg$2SI], src, cpu.tlb_shift_read);
        cpu_out$4(cpu.reg16[DX], src);
        cpu.reg$2SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read$0(seg_base + cpu.reg$2SI], src, cpu.tlb_shift_read);
        cpu_out$4(cpu.reg16[DX], src);
        cpu.reg$2SI] += add;
        cpu.reg$2CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg$2CX] != 0;
}
            */
        }, szspc, add, regspec, asize, size_endings[osize]);
    },
    "cmps": function (osize, asize) {
        var add = "-" + osize + " : " + osize,
            szspc = osize << 3;
        var regspec = asize === 16 ? "16[" : "32[E";
        funcs.push("cmps" + size_endings[osize]+ asize);
        return m(function () {
            /*
int cmps$4$3(int flags)
{
    int count = cpu.reg$2CX], add = cpu.eflags & EFLAGS_DF ? $1, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    uint$0_t dest, src;
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
        count = MAX_CYCLES_TO_RUN;
    switch(flags >> I_PREFIX_SHIFT & 3){
        case 0:
            cpu_read$0(seg_base + cpu.reg$2SI], dest, cpu.tlb_shift_read);
            cpu_read$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_read);
            cpu.reg$2DI] += add;
            cpu.reg$2SI] += add;
            cpu.lr = (int$0_t)(dest - src);
            cpu.lop2 = src;
            cpu.laux = SUB$0;
            return 0;
        case 1: // REPZ
            for (int i = 0; i < count; i++) {
                cpu_read$0(seg_base + cpu.reg$2SI], dest, cpu.tlb_shift_read);
                cpu_read$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_read);
                cpu.reg$2DI] += add;
                cpu.reg$2SI] += add;
                cpu.reg$2CX]--;

                // XXX don't set this every time
                cpu.lr = (int$0_t)(dest - src);
                cpu.lop2 = src;
                cpu.laux = SUB$0;

                //cpu.cycles_to_run--;
                if(src != dest) return 0;
            }
            return cpu.reg$2CX] != 0;
        case 2: // REPNZ
            for (int i = 0; i < count; i++) {
                cpu_read$0(seg_base + cpu.reg$2SI], dest, cpu.tlb_shift_read);
                cpu_read$0(cpu.seg_base[ES] + cpu.reg$2DI], src, cpu.tlb_shift_read);
                cpu.reg$2DI] += add;
                cpu.reg$2SI] += add;
                cpu.reg$2CX]--;
                
                // XXX don't set this every time
                cpu.lr = (int$0_t)(dest - src);
                cpu.lop2 = src;
                cpu.laux = SUB$0;

                //cpu.cycles_to_run--;
                if(src == dest) return 0;
            }
            return cpu.reg$2CX] != 0;
    }
    CPU_FATAL("unreachable");
}
            */
        }, szspc, add, regspec, asize, size_endings[osize]);
    },
    "lods": function (osize, asize) {
        var add = "-" + osize + " : " + osize,
            szspc = osize << 3;
        var regspec = asize === 16 ? "16[" : "32[E";
        var al;
        switch (osize) {
            case 1:
                al = "cpu.reg8[AL]";
                break;
            case 2:
                al = "cpu.reg16[AX]";
                break;
            case 4:
                al = "cpu.reg32[EAX]";
                break;
        }
        funcs.push("lods" + size_endings[osize]+ asize);
        return m(function () {
            /*
int lods$5$3(int flags)
{
    int count = cpu.reg$2CX], add = cpu.eflags & EFLAGS_DF ? $1, seg_base = cpu.seg_base[I_SEG_BASE(flags)];
    if ((unsigned int)count > MAX_CYCLES_TO_RUN)
        count = MAX_CYCLES_TO_RUN;
    if(!repz_or_repnz(flags)){
        cpu_read$0(seg_base + cpu.reg$2SI], $4, cpu.tlb_shift_read);
        cpu.reg$2SI] += add;
        return 0;
    }
    for (int i = 0; i < count; i++) {
        cpu_read$0(seg_base + cpu.reg$2SI], $4, cpu.tlb_shift_read);
        cpu.reg$2SI] += add;
        cpu.reg$2CX]--;
        //cpu.cycles_to_run--;
    }
    return cpu.reg$2CX] != 0;
}
            */
        }, szspc, add, regspec, asize, al, size_endings[osize]);
    }
};

var text = "";
for (var i in templates) {
    console.log(i);
    text += templates[i](1, 16);
    text += templates[i](1, 32);
    text += templates[i](2, 16);
    text += templates[i](2, 32);
    text += templates[i](4, 16);
    text += templates[i](4, 32);
}

slice_section("src/cpu/ops/string.c", "ops", text)

text = "";
for (var i = 0; i < funcs.length; i++) {
    text += "int " + funcs[i] + "(int flags);\n";
}
slice_section("include/cpu/ops.h", "string", text)

text = "";
for (var i = 0; i < funcs.length; i++) {
    text += "OPTYPE op_" + funcs[i] + "(struct decoded_instruction* i);\n";
}
slice_section("include/cpu/opcodes.h", "string", text)

text = "";
for (var i = 0; i < funcs.length; i++) {
    text += m(function () {
        /*
OPTYPE op_$0(struct decoded_instruction* i){
    int flags = i->flags, result = $0(flags);
    if(result == 0) NEXT(flags);
#ifdef INSTRUMENT
    else if(result == 1) STOP2();
    else
#endif  
    EXCEP();
}
                */
    }, funcs[i]);
}
slice_section("src/cpu/opcodes.c", "string", text)