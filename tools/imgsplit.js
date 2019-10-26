// Split image into chunks for the Emscripten target
var fs = require("fs"),
    zlib = require("zlib"),
    path = require("path");
var disk = process.argv[2],
    opt1 = process.argv[3] === "--no-gzip",
    block_size = parseInt(process.argv[4]) || 256 * 1024;
if (!disk) {
    console.error("Usage: node tools/imgsplit.js [path to image file].img [--no-gzip]");
    process.exit(0);
}

// XXX slow implementation
var block_shift = 0;
for (var i = 0; i < 32; i++) {
    if (block_size & (1 << i)) block_shift = i;
}

var image = fs.openSync(disk, "r"),
    stat = fs.statSync(disk),
    size = stat.size;

console.log(stat);
// Create directory if needed
var dir = disk.replace(/\.[^/.]+$/, "");
if (!fs.existsSync(dir)) fs.mkdirSync(dir);
else {
    // Clear all files in directory
    var dirent = fs.readdirSync(dir);
    for(var i=0;i<dirent.length;i++)
        fs.unlinkSync(path.join(dir, dirent[i]));
}

function f08x(n) {
    var y = n.toString(16);
    while (y.length !== 8) y = "0" + y;
    return y;
}

var blocks = (size + (block_size - 1)) / block_size,
    buffer = new Uint8Array(block_size), bytes=0, compressedBytes=0;
for (var i = 0; i < blocks; i++) {
    //console.log(i * block_size);
    var fileChunk = fs.readSync(image, buffer, 0, block_size, i * block_size);
    //fs.writeFileSync(path.join(dir, "blk" + f08x(i) + ".bin"), buffer);
    bytes += block_size;
    
    if(opt1) continue;
    // Automatically compress to .gz
    var compressed = zlib.deflateSync(buffer);
    compressedBytes += compressed.length;
    fs.writeFileSync(path.join(dir, "blk" + f08x(i) + ".bin.gz"), compressed);
}

console.log(bytes + " bytes written to directory");
console.log(compressedBytes + " compressed bytes written to directory");
console.log("Gzip'ed data is " + ((1-compressedBytes / bytes) * 100).toFixed(2) + "% smaller");
var options = {
    image_size: size,
    block_size: block_size
};
fs.writeFileSync(path.join(dir, "info.json"), JSON.stringify(options));

// Now write it in a binary format so that we don't have to parse the JSON. 
var i32 = new Int32Array(2);
i32[0] = size;
i32[1] = block_size;
fs.writeFileSync(path.join(dir, "info.dat"), new Buffer(i32.buffer));
