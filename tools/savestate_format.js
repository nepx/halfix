// Format a savestate for easy loading within JS
const MAGIC = 0xC8C70FF0;
const VERSION = 0;

/**
 * Read stream
 * @param {Buffer} buf 
 */
function rstream(buf) {
    this.buf = buf;
    this.pos = 0;
}
/**
 * Read unsigned byte
 */
rstream.prototype.b = function () {
    var res = this.buf[this.pos];
    this.pos = this.pos + 1 | 0;
    return res;
};
/**
 * Peek at unsigned byte
 */
rstream.prototype.pb = function () {
    var res = this.buf[this.pos];
    return res;
};
/**
 * Read unsigned dword
 */
rstream.prototype.w = function () {
    var res = this.buf.readUInt16LE(this.pos);
    this.pos = this.pos + 2 | 0;
    return res;
};
/**
 * Read signed dword
 */
rstream.prototype.d = function () {
    var res = this.buf.readInt32LE(this.pos);
    this.pos = this.pos + 4 | 0;
    return res;
};
/**
 * Read unsigned dword
 */
rstream.prototype.ud = function () {
    var res = this.buf.readInt32LE(this.pos);
    this.pos = this.pos + 4 | 0;
    return res >>> 0;
};
rstream.prototype.rs = function () {
    var str = "";
    while (true) {
        var chr = this.b();
        if (!chr) break;
        str += String.fromCharCode(chr);
    }
    return str;
};

function fatal(msg) {
    console.error("FATAL: " + msg);
    process.exit(1);
}

var pathbase = process.argv[2], path = require("path"),
    zlib = require("zlib");
if (!pathbase) {
    console.error("Usage: node " + process.argv[1] + " <path to savestate directory>");
    process.exit(1);
}
var fs = require("fs"), r = new rstream(fs.readFileSync(path.join(pathbase, "state.bin")));

if (r.ud() !== MAGIC) fatal("Invalid magic number");
if (r.d() !== VERSION) fatal("Invalid magic number");

var state = {};
function parse_obj(cur_object) {
    if (r.b() !== 1) fatal("not an object");
    var nelts = r.b() & 0xFF;
    for (var i = 0; i < nelts; i = i + 1 | 0) {
        var name = r.rs();
        var type = r.pb();
        if (type === 1) {
            // Object
            var obj = {};
            parse_obj(obj, cur_object);
            cur_object[name] = obj;
        } else {
            // Field
            r.b(); // skip type -- we already know that it's a field
            var len = r.d();
            var data = r.buf.slice(r.pos, r.pos + len | 0);
            r.pos = r.pos + len | 0;
            cur_object[name] = data;
        }
    }
}
parse_obj(state);

function format(x, arg) {
    var str = arg.toString(16);
    while (str.length < 8) str = "0" + str;
    return x.replace("%08x", str);
}

var ide_data = {};
function ide_parse(ide_ident) {
    var ide = state[ide_ident];
    if (!ide) return;
    var size = ide.size.readInt32LE(0),
        blk_count = ide.block_count.readInt32LE(0),
        path_count = ide.path_count.readInt32LE(0), paths = [];
    for (var i = 0; i < path_count; i = i + 1 | 0) {
        var idebuf = ide["path" + i];
        var str = idebuf.toString("utf8", 0, idebuf.length - 1);
        paths.push(str);
    }
    var arr = ide.block_array, block_array = [];
    for (var i = 0; i < arr.length; i = i + 4 | 0) {
        block_array.push(arr.readInt32LE(i));
    }
    var obj = {
        size: size,
        blk_count: blk_count,
        paths: paths,
        block_array: block_array
    };
    ide_data[ide_ident] = obj;

    for (var i = 0; i < block_array.length; i = i + 1 | 0) {
        var idx = block_array[i];
        var path$ = path.join(paths[idx], format("blk%08x.bin", i));
        if (!fs.existsSync(path$ + ".gz")) {
            var data = fs.readFileSync(path$);
            fs.writeFileSync(path$ + ".gz", zlib.deflateSync(data));
        }
    }
}
// Separate out the ide%d-%d parts
ide_parse("ide0-0");
ide_parse("ide0-1");
ide_parse("ide1-0");
ide_parse("ide1-1");
function writeFile(name, data) {
    fs.writeFileSync(path.join(pathbase, name), data);
    fs.writeFileSync(path.join(pathbase, name + ".gz"), zlib.deflateSync(data));
}
writeFile("diskinfo.json", JSON.stringify(ide_data));

function compress(name) {
    var data = fs.readFileSync(path.join(pathbase, name));
    fs.writeFileSync(path.join(pathbase, name + ".gz"), zlib.deflateSync(data));
}
compress("ram");
compress("vram");
compress("state.bin");