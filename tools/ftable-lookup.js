// A small script to find out what integer value corresponds to what value
// Usage:
//  node tools/ftable-lookup.js <index> [signature, defaults to 'ii']
var idx = eval(process.argv[2]), sig = process.argv[3] || "ii";
var fs = require("fs");
var lines = fs.readFileSync("halfix.js").toString().split("\n"), n = "", v = false;
for(var i=0;i<lines.length;i++){
    var line = lines[i];
    if(v && line.indexOf("];") !== -1){
        n += line;
        break;
    }
    if(v){
        n += line + "\n";
    }
    if(line.indexOf("var FUNCTION_TABLE_" + sig + " ") !== -1){
        v = true;
        n += line + "\n";
    }
}
var line = n.replace(/\n/g, "").split(",");
console.log(line[idx]);