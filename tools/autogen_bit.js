// Automatically generate bit operations
var core = require("./autogen");
var slice_section = core.slice_section;

var file = "",
    funcs = [];

function prettify(str) {
    var lines = str.split("\n"),
        res = [];
    var indent = 0;
    for (var i = 0; i < lines.length; i++) {
        var line = lines[i].trim();
        if (!line) continue;
        console.log(line, indent);
        var tempIndent = 0;
        if (line[0] === "#") {
            tempIndent = indent;
            indent = 0;
        }
        if (line.indexOf("}") !== -1) indent--;
        for (var j = 0; j < indent; j++) line = "    " + line;
        if (line.indexOf("{") !== -1) indent++;
        res.push(line);
        if (line[0] === "#")
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

function generate_reg(name, size, ptr) {
    file += m(function () {
        /*
OPTYPE op_$0_r$1(struct decoded_instruction* i){
    uint32_t flags = i->flags;
    $0$1($2R$1(I_RM(flags)), (R$1(I_REG(flags)) & i->disp$1) + i->imm8);
    NEXT(flags);
}
        */
    }, name, size,  ptr ? "&" : "");
    funcs.push("op_" + name + "_r" + size);
}

function addr(a, size){
    if(size === 16)
        return "((" + a + " / 16) * 2)";
    else 
        return "((" + a + " / 32) * 4)";
}

function generate_bt_mem(size) {
    file += m(function () {
        /*
OPTYPE op_bt_e$0(struct decoded_instruction* i){
    uint32_t flags = i->flags, x = I_OP2(flags) ? i->imm8 : R$0(I_REG(flags)), linaddr = cpu_get_linaddr(flags, i), dest;
    cpu_read$0(linaddr + $1, dest, cpu.tlb_shift_read);
    bt$0(dest, x);
    NEXT(flags);
}
        */
    }, size, addr("x", size));
    funcs.push("op_bt_e" + size);
}
function generate_bt_mem_rmw(method, size) {
    file += m(function () {
        /*
OPTYPE op_$1_e$0(struct decoded_instruction* i){
    uint32_t x = I_OP2(i->flags) ? i->imm8 : R$0(I_REG(i->flags));
    arith_rmw3($0, $1$0, $2, x);
    NEXT(flags);
}
        */
    }, size, method, addr("x", size));
    funcs.push("op_" + method + "_e" + size);
}

for (var i = 0; i < 2; i++) {
    generate_reg("bt", 16 << i, false);
    generate_reg("bts", 16 << i, true);
    generate_reg("btc", 16 << i, true);
    generate_reg("btr", 16 << i, true);
}
generate_bt_mem(16);
generate_bt_mem(32);

for (var i = 0; i < 2; i++) {
    generate_bt_mem_rmw("bts", 16 << i, true);
    generate_bt_mem_rmw("btc", 16 << i, true);
    generate_bt_mem_rmw("btr", 16 << i, true);
}

slice_section("src/cpu/opcodes.c", "bit", file);

file = "";
for(var i=0;i<funcs.length;i++) file += "OPTYPE " + funcs[i] + "(struct decoded_instruction* i);\n";
slice_section("include/cpu/opcodes.h", "bit", file);