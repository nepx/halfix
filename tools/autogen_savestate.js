// Given a struct and fields, create a savestate function. This saves a lot of time, especially if you're messing around with device models. 
var core = require("./autogen");
var slice_section = core.slice_section
var get_section = core.get_section;
var parseEnum = core.parseEnum;

var fs = require("fs");
var dirbase = "src/hardware/";

var types = ["uint8_t", "int8_t", "uint16_t", "int16_t", "uint32_t", "int32_t", "uint64_t", "int64_t", "itick_t", "time_t", "int", "void", "unsigned int"];
var tl = types.length;
for (var i = 0; i < tl; i++) types.push(types[i] + "*");
var type_size = {
    "uint8_t": 1,
    "int8_t": 1,
    "uint16_t": 2,
    "int16_t": 2,
    "uint32_t": 4,
    "int64_t": 8,
    "uint64_t": 8,
    "int32_t": 4,
    "itick_t": 8,
    "time_t": "sizeof(time_t)",
    "int": 4,
    "unsigned int": 4
};
var file_struct_names = {
    "cmos.c": "cmos",
    "dma.c": "dma",
    "vga.c": "vga",
    "kbd.c": "kbd",
    "pic.c": "pic.ctrl[NUMBER]",
    "pit.c": "pit.chan[NUMBER]",
    "ide.c": "ide[NUMBER]",
    "cpu.h": "cpu",
    "fpu.c": "fpu",
    "ioapic.c": "ioapic",
    "apic.c": "apic",
    "pci.c": "pci",
    "fdc.c": "fdc",
    "acpi.c": "acpi"
};

function parseDirective(n) {
    var spl = n.split(":");
    spl[0] = spl[0].trim();
    switch (spl[0]) {
        case "ignore":
            toIgnore = spl[1].trim().split(", ");
            break;
        default:
            console.error("Unknown directive: " + spl[0]);
            process.exit(0);
    }
}

// A primitive C tokenizer
function tokenize(section) {
    var toks = [],
        str = "";
    for (var i = 0; i < section.length; i++) {
        var nchr = section.charAt(i);
        if (nchr === "/") {
            if (section.charAt(i + 1) === "*") {
                i += 2;
                while (i < section.length) {
                    if (section.charAt(i) === "*" && section.charAt(i + 1) === "/") {
                        i += 2;
                        break;
                    }
                    i++;
                }
            } else if (section.charAt(i + 1) === "/") {
                if (section.charAt(i + 2) === "/") {
                    i += 3;
                    var tempStr = "";
                    while (i < section.length) {
                        if (section.charAt(i) === "\n") break;
                        tempStr += section.charAt(i++);
                    }
                    parseDirective(tempStr);
                } else {
                    while (i < section.length) {
                        if (section.charAt(i) === "\n") break;
                        i++;
                    }
                }
            }
            continue;
        }
        if (!(/\s/.test(nchr))) {
            var spl = false;
            switch (nchr) {
                case ",":
                case ";":
                case "[": // arrays
                case "]":
                case "{": // unions
                case "}":
                case "*":
                    spl = true;
                    break;
            }
            if (spl) {
                if (str) toks.push(str);
                toks.push(nchr);
                str = "";
            } else str += nchr;
        } else {
            if (str) toks.push(str);
            str = "";
        }
    }
    if (str) toks.push(str);
    return toks;
}
var tokGlobal = [],
    tokPtr = 0,
    entries = [],
    toIgnore = [];

function addEntry(n) {
    entries.push(n);
}

function reset() {
    tokGlobal = [];
    tokPtr = 0;
    fields = [];
    toIgnore = [];
}

function next() {
    return tokGlobal[tokPtr++];
}

function at() {
    return tokGlobal[tokPtr];
}

function eat() {
    tokPtr++;
}

function eat2(n) {
    if (is(n)) eat();
}

function end() {
    return tokPtr >= tokGlobal.length;
}

function readType() {
    var n = next();
    switch (n) {
        case "struct":
        case "unsigned":
            n += " " + next();
            break;
        default:
            if (types.indexOf(n) === -1) throw new Error("Unknown type: " + n);
    }
    while (is("*")) {
        eat();
        n += "*";
    }
    return n;
}

function mustBe(n) {
    var n2 = next();
    if (n !== n2) throw new Error("Expected " + n + " got " + n2);
}

function is(n) {
    return at() === n;
}

function StructField(name, type, size) {
    this.name = name;
    this.type = type;
    this.size = size || -1;
}

function parse(fields, fn) {
    reset();
    tokGlobal = tokenize(fields);
    //console.log(tokGlobal);
    var dscID = file_struct_names[fn];
    var inUnion = false;

    while (!end()) {
        if (inUnion) {
            while (!is("}")) eat();
            mustBe("}");
            // If we use the __attribute__ keyword, then we just skip up to the semicolon
            if(is("__attribute__((aligned(16)))")) while(!is(";")) eat();
            mustBe(";");
            inUnion = false;
        }
        switch (at()) {
            case "union":
                eat();
                mustBe("{");
                inUnion = true;
        }
        var type = readType();
        while (!is(";")) {
            // XXX
            var name = next(),
                size = undefined,
                escape = false;
            if (name === "*") name = next();
            switch (at()) {
                case ";":
                    escape = true;
                case ",": // intentional fallthrough
                    break;
                case "[":
                    eat();
                    var nToks = 0;
                    var y = [];
                    while (!is("]")) {
                        nToks++;
                        y.push(next());
                    }
                    if (nToks === 1) size = y.join(" ");
                    else size = "(" + y.join(" ") + ")";
                    mustBe("]");
                    break;
            }
            if (toIgnore.indexOf(name) === -1) {
                var struct = new StructField(dscID + "." + name, type, size);
                //console.log(struct);
                addEntry(struct);
            }
            size = undefined;
            eat2(",");
            if (escape) break;
        }
        mustBe(";");
    }
}

function isNum(val) {
    return /^\d+$/.test(val);
}

function szShift(n) {
    if (typeof n === "number") return n << 1;
    else return n + " * 2";
}

function namespace(e){
    return e.split(".")[0];
}
function field(e){
    var y = e.split(".");
    y.shift();
    return y.join(".");
}

var typeTranslations = {
    "uint8_t": "u8",
    "uint16_t": "u16",
    "uint32_t": "u32",
    "int": "u32",
};

// XXX nasty manual hack
var adds = {
    "pic": 1,
    "pit": 2,
    "kbd": 6,
    "ioapic": 0,
    "apic": 0,
    "fpu": 16, // 8 fp registers * (1 exp + 1 frac)
    "pci": "n"
};

function addsFunc(n){
    if(typeof n === "undefined") return "";
    else return " + " + n;
}
function imult(x, name){
    var max = 1;
    if(name.indexOf("NUMBER") !== -1) {
    if (name.indexOf("pic") !== -1 || name.indexOf("ide") !== -1) max = 2;
    else max = 3; // pit
    }
    if(max === 1) return x;
    else return "(" + x + ") * " + max;
}

function writeOut(y) {
    var result = [];
    result.push("    struct bjson_object* obj = state_obj(\""
         + namespace(entries[0].name) +"\", "
          + imult(
              (entries.length) + addsFunc((adds[namespace(entries[0].name)])),
              entries[0].name
              )
           + ");");
    for (var i = 0; i < entries.length; i++) {
        var entry = entries[i];
        if(namespace(entries[0].name) === "apic") console.log(entry);
        if (entry.size !== -1) { // Array type
            var max = 1;
            if (entry.name.indexOf("apic") === -1 && entry.name.indexOf("pic") !== -1 || entry.name.indexOf("ide") !== -1) max = 2;
            for (var u = 0; u < max; u++) {
                var size = type_size[entry.type],
                    realSize;
                if ((entry.size)) realSize = eval(entry.size) * size;
                else realSize = entry.size + " * " + size;
                var name = entry.name.replace("NUMBER", u);
                //console.log(type_size[entry.type]);
                result.push("    state_field(obj, " + realSize + ", \"" + name + "\", &" + name + ");");
                //result.push("    state_array(" + realSize + ", \"" + name + "\", &" + name + ");");
            }
        } else {
            //console.log(entry.type);
            var size = type_size[entry.type];
            if (entry.name.indexOf("NUMBER") === -1)
                result.push("    state_field(obj, " + size + ", \"" + entry.name + "\", &" + entry.name + ");");
            else {
                if (entry.name.indexOf("apic") === -1 && entry.name.indexOf("pic") !== -1 || entry.name.indexOf("ide") !== -1) max = 2;
                else max = 3; // pit
                for (var u = 0; u < max; u++) {
                    var name = entry.name.replace("NUMBER", u);
                    result.push("    state_field(obj, " + size + ", \"" + name + "\", &" + name + ");");
                }
            }
        }
    }
    slice_section(y, "state", result.join("\n"));
    entries = [];
}

fs.readdir(dirbase, function (err, items) {
    if (err) throw err;
    console.log(items);
    for (var $ = 0; $ < items.length; $++) {
        var fn = items[$];
        if (fn.indexOf("-old") !== -1) continue;
        if (fn.indexOf("-backup") !== -1) continue;
        //if (fn.indexOf("ide.c") !== -1) continue; // Done manually
        var fields = get_section(dirbase + fn, "STRUCT", "struct");

        parse(fields, fn);
        writeOut(dirbase + fn);
    }
});
parse(get_section("include/cpu/cpu.h", "STRUCT", "struct"), "cpu.h");
writeOut("src/cpu/cpu.c");
parse(get_section("src/cpu/fpu.c", "STRUCT", "struct"), "fpu.c");
writeOut("src/cpu/fpu.c");