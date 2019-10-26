// Automatically generate JCC opcode definitions
var core = require("./autogen");
var slice_section = core.slice_section;

var handlers = {
    "o": "cpu_get_of()",
    "no": "!cpu_get_of()",
    "b": "cpu_get_cf()",
    "nb": "!cpu_get_cf()",
    "z": "cpu_get_zf()",
    "nz": "!cpu_get_zf()",
    "be": "cpu_get_zf() || cpu_get_cf()",
    "nbe": "!(cpu_get_zf() || cpu_get_cf())",
    "s": "cpu_get_sf()",
    "ns": "!cpu_get_sf()",
    "p": "cpu_get_pf()",
    "np": "!cpu_get_pf()",
    "l": "cpu_get_sf()!=cpu_get_of()",
    "nl": "cpu_get_sf()==cpu_get_of()",
    "le": "cpu_get_zf()||(cpu_get_sf()!=cpu_get_of())",
    "nle": "!cpu_get_zf()&&(cpu_get_sf()==cpu_get_of())"
};

var txt = [],
    indent = 0;

function x(a) {
    a = a.trim();
    for (var i = 0; i < indent; i++)
        a = "    " + a;
    txt.push(a);
}

for (var i in handlers) {
    x("OPTYPE op_j" + i + "16(struct decoded_instruction* i) {");
    indent++;
    x("jcc16(" + handlers[i] + ");");
    indent--;
    x("}");
    x("OPTYPE op_j" + i + "32(struct decoded_instruction* i) {");
    indent++;
    x("jcc32(" + handlers[i] + ");");
    indent--;
    x("}");
}

slice_section("src/cpu/opcodes.c", "jcc", txt.join("\n"));

txt = [];
for (var i in handlers) {
    x("OPTYPE op_j" + i + "16(struct decoded_instruction* i);");
    x("OPTYPE op_j" + i + "32(struct decoded_instruction* i);");
}
slice_section("include/cpu/opcodes.h", "jcc", txt.join("\n"));