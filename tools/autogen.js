// Core autogen utilities
var fs = require("fs");
function fatal(msg) {
    console.error(msg);
    process.exit(1);
}

// Given a file with <<< AUTOGENERATE >>> sections, slice everything inside the section out and add "replacement"
function slice_section(file, section_name, replacement) {
    var data = fs.readFileSync(file) + "";
    var section_text = "\\/\\/ <<< BEGIN AUTOGENERATE \\\"" + section_name + "\\\" >>>";
    var section_length = section_text.length - 4;
    var begin = data.match(new RegExp(section_text));
    if (begin === null) {
        fatal("Could not find autogen section \"" + section_name + "\" within file \"" + file + "\"");
    }
    var end = data.match(new RegExp("\\/\\/ <<< END AUTOGENERATE \\\"" + section_name + "\\\" >>>"));
    if (end === null) {
        fatal("Could not find end of autogen section \"" + section_name + "\" within file \"" + file + "\"");
    }
    /*
    console.log(begin.index, end.index);
    console.log(data.slice(0, begin.index + section_length));
    console.log(data.slice(end.index));
    */
    var newdata = data.slice(0, begin.index + section_length) +
        "\n" +
        replacement + "\n" + data.slice(end.index);
    fs.writeFileSync(file, newdata);
    //fs.writeFileSync(file + "-backup", data);
}

module.exports.slice_section= slice_section;

function get_section(file, type, section_name) {
    var data = fs.readFileSync(file) + "";
    type = type.toUpperCase();
    var section_text = "\\/\\/ <<< BEGIN " + type + " \\\"" + section_name + "\\\" >>>";
    var section_length = section_text.length - 4;
    var begin = data.match(new RegExp(section_text));
    if (begin === null) {
        fatal("Could not find " + type + " section \"" + section_name + "\" within file \"" + file + "\"");
    }
    var end = data.match(new RegExp("\\/\\/ <<< END " + type + " \\\"" + section_name + "\\\" >>>"));
    if (end === null) {
        fatal("Could not find end of " + type + " section \"" + section_name + "\" within file \"" + file + "\"");
    }
    return data.slice(begin.index + section_length, end.index);
}
module.exports.get_section= get_section;
// Parses an enum value and strips single-line comments from it
function parseEnum(sectionText) {
    var names = [];
    var i = 0,
        current = "";
    while (i < sectionText.length) {
        if (sectionText[i] === "/")
            while (sectionText[i] !== "\n" && i < sectionText.length) i++;
        else if (sectionText[i] === ",") {
            names.push(current.trim());
            current = "";
        } else current += sectionText[i];
        i++;
    }
    if (current.trim()) names.push(current.trim());
    return names;
}
module.exports.parseEnum= parseEnum;
