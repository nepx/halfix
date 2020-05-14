// Halfix emulator loader and minimal runtime initializer.

// Some URL query parameters you may find useful:
//  ?app=file.js  -  Load Halfix from path "file.js" (default: halfix.js)
//  ?hd[a|b|c|d]=imgdir  -  Set hard disk image (none by default)
//  ?cd[a|b|c|d]=imgdir  -  Set CD-ROM disk image (none by default)
//  ?fd[a|b]=imgdir  -  Set floppy disk image (none by default)
//  ?pcienabled=true  -  Enable PCI support (enabled by default)
//  ?apicenabled=true  -  Enable APIC support (enabled by default)
//  ?bios=file.bin  -  Load BIOS image from "file.bin" (default: bios.bin)
//  ?vgabios=file.bin  - Load VGA BIOS image from "file.bin" (default: vgabios.bin)
//  ?now=time  - Set emulator time seen in emulator (default: 29 July 2019)
//  ?mem=32  - Set size of emulated RAM (default: 32 MB)
//  ?fast=[0|1]  - Ignore HLT timings, run as fast as possible (default: 0)
//  ?pcivga=true  - Enable PCI VGA support (this allows more systems to detect VESA screens, but is severely limited)

function $(e) {
    return document.getElementById(e);
}

var printElt = $("log"),
    netstat = $("netstat"),
    totalbytesElt = $("totalbytes");
const SAVE_LOGS = false;
var arr = [];
var Module = {
    "canvas": $("screen"),
    "print": function (ln) {
        printElt.value += ln + "\n";
    },
    "printErr": function (ln) {
        if (SAVE_LOGS)
            arr.push(ln);
    }
};

function save(filename, data) {
    var blob = new Blob([data], { type: 'text/csv' });
    if (window.navigator.msSaveOrOpenBlob) {
        window.navigator.msSaveBlob(blob, filename);
    }
    else {
        var elem = window.document.createElement('a');
        elem.href = window.URL.createObjectURL(blob);
        elem.download = filename;
        document.body.appendChild(elem);
        elem.click();
        document.body.removeChild(elem);
    }
}
function saveLog() {
    save("test.txt", arr.join("\n"));
}

(function () {
    // ============================================================================
    // API functions
    // ============================================================================
    var image_data = null,
        ctx = Module["canvas"].getContext("2d");
    window["update_size"] = function (fbptr, x, y) {
        if (x == 0 || y == 0) return; // Don't do anything if x or y is zero (VGA resizing sometimes gives weird sizes)
        Module["canvas"].width = x;
        Module["canvas"].height = y;
        image_data = new ImageData(new Uint8ClampedArray(Module["HEAPU8"].buffer, fbptr, (x * y) << 2), x, y);
    };

    window["update_screen"] = function () {
        ctx.putImageData(image_data, 0, 0);
    };

    function fatal(msg) {
        $("messages").innerHTML = "FATAL: " + msg;
        throw msg;
    }

    function loadEmulator(path) {
        var script = document.createElement("script");
        script.src = path;
        document.head.appendChild(script);
    }

    function getParameterByName(name, url) {
        if (!url) url = window.location.href;
        name = name.replace(/[\[\]]/g, '\\$&');
        var regex = new RegExp('[?&]' + name + '(=([^&#]*)|&|#|$)'),
            results = regex.exec(url);
        if (!results) return null;
        if (!results[2]) return '';
        return decodeURIComponent(results[2].replace(/\+/g, ' '));
    }

    // ============================================================================
    // Network loading functions
    // ============================================================================
    var netstat = $("netstat"),
        netfile = $("netfile"),
        netstat_to_load;
    /**
     * Updates how far the progress bar has gone
     * @param {number} amount 
     */
    function updateNetworkProgress(amount) {
        netstat.value += amount;
    }

    var totalbytes = 0; // Total bytes loaded from network sources
    /**
     * Update total number of bytes read from network sources
     * @param {number} x 
     */
    function updateTotalBytes(x) {
        totalbytes = x + totalbytes | 0;
        totalbytesElt.innerHTML = (totalbytes / 1024 / 1024).toFixed(2) + " MB";
    }

    var inLoading = false,
        savestateRequested = false;

    function _handle_savestate() {
        if (savestateRequested) saveState();
        savestateRequested = false;
    }
    /**
     * @param {string[]||object[]} paths
     * @param {function(object, Uint8Array[])} cb
     * @param {boolean=} gz Are the files to be fetched gzip'ed?
     */
    function loadFiles(paths, cb, gz) {
        var resultCounter = paths.length | 0,
            results = [];

        // If we are loading more than one file, then make sure that the progress bar is wide enough
        netstat.max = paths.length * 100 | 0;
        netstat.value = 0;
        netfile.innerHTML = paths.join(", ");
        inLoading = true;
        for (var i = 0; i < paths.length; i = i + 1 | 0) {
            (function () {
                // Save some state information inside the closure.
                var xhr = new XMLHttpRequest(),
                    idx = i,
                    lastProgress = 0;
                var path = paths[i] + (gz ? ".gz" : "");
                xhr.open("GET", paths[i] + (gz ? ".gz" : ""));

                xhr.onprogress = function (e) {
                    if (e.lengthComputable) {
                        var now = e.loaded / e.total * 100 | 0;
                        updateNetworkProgress(now - lastProgress | 0);
                        lastProgress = now;
                    }
                };
                xhr.responseType = "arraybuffer";
                xhr.onload = function () {
                    if (!gz)
                        results[idx] = new Uint8Array(xhr.response);
                    else
                        results[idx] = pako.inflate(new Uint8Array(xhr.response));
                    resultCounter = resultCounter - 1 | 0;
                    updateTotalBytes(xhr.response.byteLength | 0);
                    if (resultCounter === 0) {
                        cb(null, results);

                        inLoading = false;
                        _handle_savestate();
                    }
                };
                xhr.onerror = function (e) {
                    alert("Unable to load file");
                    cb(e, null);
                };
                xhr.send();
            })();
        }
    }
    /**
     * @param {string} p Path to normalize
     * @return {string} Normalized path
     */
    function normalize_path(p) {
        var up = 0;
        var parts = p.split("/");
        var prefix = "";
        if (p.charAt(0) === "/") {
            prefix = "/"
        }
        for (var i = parts.length; i--;) {
            var last = parts[i];
            if (last.length === 0)
                parts.splice(i, 1); // We don't like empty directory names
            if (last === ".")
                parts.splice(i, 1);
            else if (last === "..") {
                parts.splice(i, 1);
                up++;
            } else if (up) {
                parts.splice(i, 1);
                up--;
            }
        }
        return prefix + parts.join("/");
    };
    /**
     * Join two fragments of a path together
     * @param {string} a The first part of the path
     * @param {string} b The second part of the path
     */
    function join_path(a, b) {
        if (b.charAt(0) !== "/")
            b = "/" + b;
        if (a.charAt(a.length - 1 | 0) === "/")
            a = a.substring(0, a.length - 1 | 0);
        return a + b; //normalize_path(a + b);
    }

    // Emscripten heap views
    /** @type {Uint8Array} */
    var u8,
        /** @type {Uint16Array} */
        u16,
        /** @type {Int32Array} */
        i32;


    function getBooleanByName(x, d) {
        var param = getParameterByName(x);
        d = typeof d === "undefined" ? 1 : d;
        if (typeof param !== "string") return d | 0;
        return (param === "true") | 0;
    }

    function getIntegerByName(x, v2) {
        var param = getParameterByName(x);
        if (typeof param !== "string") return v2;
        return parseInt(param) | 0;
    }

    function getDoubleByName(x, v2) {
        var param = getParameterByName(x);
        if (typeof param !== "string") return v2;
        return +parseFloat(param);
    }

    function buildDrive(config, drvid, primary, master) {
        var hd = getParameterByName("hd" + drvid),
            cd = getParameterByName("cd" + drvid);
        if (!hd && !cd)
            return;
        config.push("[ata" + primary + "-" + master + "]");
        if (hd) {
            if (hd !== "none") {
                config.push("file=" + hd);
                config.push("inserted=1");
            }
            config.push("type=hd");
        } else {
            if (cd !== "none") {
                config.push("file=" + cd);
                config.push("inserted=1");
            }
            config.push("type=cd");
        }
    }

    function buildFloppy(config, drvid) {
        var fd = getParameterByName("fd" + drvid);
        if (!fd)
            return;
        config.push("[fd" + drvid + "]");
        config.push("file=" + fd);
        config.push("inserted=1");
    }

    /**
     * From the given URL parameters, we build a .conf file for Halfix to consume. 
     */
    function buildConfiguration() {
        var config = [];
        /*

        bios_path: getParameterByName("bios") || "bios.bin",
        bios: null,
        vgabios_path: getParameterByName("vgabios") || "vgabios.bin",
        vgabios: null,
        hd: [getParameterByName("hda"), getParameterByName("hdb"), getParameterByName("hdc"), getParameterByName("hdd")],
        cd: [getParameterByName("cda"), getParameterByName("cdb"), getParameterByName("cdc"), getParameterByName("cdd")],
        pci: getBooleanByName("pcienabled"),
        apic: getBooleanByName("apicenabled"),
        acpi: getBooleanByName("apicenabled"),
        now: getParameterByName("now") ? parseFloat(getParameterByName("now")) : 1563602400,
        mem: getParameterByName("mem") ? parseInt(getParameterByName("mem")) : 32,
        vgamem: getParameterByName("vgamem") ? parseInt(getParameterByName("vgamem")) : 32,
        fd: [getParameterByName("fda"), getParameterByName("fdb")],
        boot: getParameterByName("boot") || "chf" // HDA, FDC, CDROM
        */
        config.push("bios=" + (getParameterByName("bios") || "bios.bin"));
        config.push("vgabios=" + (getParameterByName("vgabios") || "vgabios.bin"));
        config.push("pci=" + getBooleanByName("pcienabled"));
        config.push("apic=" + getBooleanByName("apicenabled"));
        config.push("acpi=" + getBooleanByName("acpienabled"));
        config.push("pcivga=" + getBooleanByName("pcivga", 0));
        config.push("now=" + getDoubleByName("now", new Date().getTime()));
        var floppyRequired = (!!getParameterByName("fda") || !!getParameterByName("fdb")) | 0;
        console.log(floppyRequired);
        config.push("floppy=" + floppyRequired);
        var mem = getIntegerByName("mem", 32),
            vgamem = getIntegerByName("vgamem", 4);

        function roundUp(v) {
            v--;
            v |= v >> 1;
            v |= v >> 2;
            v |= v >> 4;
            v |= v >> 8;
            v |= v >> 16;
            v++;
            return v;
        }
        //Module["TOTAL_MEMORY"] = roundUp(mem + 32 + vgamem) * 1024 * 1024 | 0;
        config.push("memory=" + mem + "M");
        config.push("vgamemory=" + vgamem + "M");
        buildDrive(config, "a", 0, "master");
        buildDrive(config, "b", 0, "slave");
        buildDrive(config, "c", 1, "master");
        buildDrive(config, "d", 1, "slave");

        buildFloppy(config, "a");
        buildFloppy(config, "b");
        config.push("[boot]");

        var bootOrder = getParameterByName("boot") || "chf";
        config.push("a=" + bootOrder[0] + "d");
        config.push("b=" + bootOrder[1] + "d");
        config.push("c=" + bootOrder[2] + "d");

        return config.join("\n");
    }

    var _allocs = [];
    /**
     * Allocates a patch of memory in Emscripten. Returns the address pointer
     * @param {number} size 
     * @return {number} Address
     */
    function alloc(size) {
        var n = Module["_malloc"](size);
        _allocs.push(n);
        return n;
    }

    /**
     * Copy a string into memory
     * @param {number} dest 
     * @param {string} src 
     */
    function strcpy(dest, src) {
        var srclen = src.length | 0;
        for (var i = 0; i < srclen; i = i + 1 | 0)
            u8[i + dest | 0] = src.charCodeAt(i);
        u8[dest + srclen | 0] = 0; // End with NULL terminator
    }
    /**
     * Copy a Uint8Array into memory
     * @param {number} dest 
     * @param {Uint8Array} src 
     */
    function memcpy(dest, src) {
        var srclen = src.length | 0;
        for (var i = 0; i < srclen; i = i + 1 | 0)
            u8[i + dest | 0] = src[i | 0];
    }

    /**
     * Frees every single patch of memory we have reserved with alloc()
     */
    function gc() {
        var free = Module["_free"];
        for (var i = 0; i < _allocs.length; i = i + 1 | 0) free(_allocs[i]);
        _allocs = [];
    }

    /**
     * Call an Emscripten function pointer with the signature void func(int, int);
     * @param {number} cb The function pointer itself
     * @param {number} cb_ptr The first argument
     * @param {number} arg2 The second argument
     */
    function fptr_vii(cb, cb_ptr, arg2) {
        Module["dynCall_vii"](cb, cb_ptr, arg2);
    }
    /**
     * Copy a string into JavaScript
     * @param {number} src
     */
    function readstr(src) {
        var str = "";
        while (u8[src] !== 0) {
            str += String.fromCharCode(u8[src]);
            src = src + 1 | 0;
        }
        return str;
    }

    // Returns a pointer to an Emscripten-compiled function
    function wrap(nm) {
        return Module["_" + nm];
    }

    /**
     * Create a disk image and set up Emscripten state
     * @param {number} diskid 
     * @param {string} path 
     * @param {Uint8Array} info 
     * @constructor
     */
    function RemoteHardDiskImage(diskid, path, info) {
        /** @type {string} */
        this.path = path;
        /** @type {Uint8Array} */
        this.info = info;
        /** @type {string} */
        this.diskid = diskid;
        /** @type {Uint8Array[]} */
        this.blocks = [];

        // URLs to load data from
        /** @type {string[]} */
        this.request_queue = [];
        /** @type {number[]} */
        this.request_queue_ids = [];
        /** @type {number[][]} */
        this.request_queue_args = [];

        // For Emscripten function callbacks
        /** @type {string} */
        this.cb = -1;
        /** @type {string} */
        this.arg1 = -1;

        window["drives"][diskid] = this;
    }

    /**
     * Call an Emscripten callback
     * @type {number} res
     */
    RemoteHardDiskImage.prototype.callback = function (res) {
        fptr_vii(this.cb | 0, this.arg1 | 0, res | 0);
    };

    /**
     * Adds a block to the cache
     * 
     * @param {number} id
     * @param {number} offset Offset in memory to read from
     * @param {number} length 
     */
    RemoteHardDiskImage.prototype["addCache"] = function (id, offset, length) {
        this.blocks[id] = u8.slice(offset, length + offset | 0);
    };

    /**
     * Reads a section of a block from the cache
     * 
     * @param {number} id Block ID
     * @param {number} buffer Position in the buffer
     * @param {number} offset Offset in the block to read from
     * @param {number} length Number of bytes to read
     */
    RemoteHardDiskImage.prototype["readCache"] = function (id, buffer, offset, length) {
        id = id | 0;
        if (!this.blocks[id]) {
            printElt.value += "[JSError] readCache(id=0x" + id.toString(16) + ", buffer=0x" + buffer.toString(16) + ", length=0x" + buffer.toString(16) + ")\n";
            return 1; // No block here with that data.
        }
        var buf = this.blocks[id].subarray(offset, length + offset | 0);
        if (buf.length > length) throw new Error("Block too long");
        u8.set(this.blocks[id].subarray(offset, length + offset | 0), buffer);
        return 0;
    };
    /**
     * Reads a section of a block from the cache
     * 
     * @param {number} id Block ID
     * @param {number} buffer Position in the buffer
     * @param {number} offset Offset in the block to read from
     * @param {number} length Number of bytes to read
     */
    RemoteHardDiskImage.prototype["writeCache"] = function (id, buffer, offset, length) {
        id = id | 0;
        if (!this.blocks[id]) {
            printElt.value += "[JSError] writeCache(id=0x" + id.toString(16) + ", buffer=0x" + buffer.toString(16) + ", length=0x" + buffer.toString(16) + ")\n";
            return 1; // No block here with that data.
        }
        var buf = u8.subarray(buffer, length + buffer | 0);
        if (buf.length > length) throw new Error("Block too long");
        this.blocks[id].set(buf, offset);
        return 0;
    };

    /**
     * Queues a memory read
     * 
     * @param {number} str Pointer to URL
     * @param {number} id Block ID to store it in
     */
    RemoteHardDiskImage.prototype["readQueue"] = function (str, id) {
        this.request_queue.push(readstr(str));
        this.request_queue_ids.push(id);
    };

    var req = 0;
    /**
     * Runs all requests simultaneously.
     * 
     * @param {number} cb Callback pointer
     * @param {number} arg1 Callback argument
     */
    RemoteHardDiskImage.prototype["flushReadQueue"] = function (cb, arg1) {
        req++;
        // TODO: cache requsts
        //if (req > 5) throw "stop g";
        /** @type {RemoteHardDiskImage} */
        var me = this;

        this.cb = cb;
        this.arg1 = arg1;

        loadFiles(this.request_queue, function (err, data) {
            if (err) throw err;

            var rql = me.request_queue.length;
            for (var i = 0; i < rql; i = i + 1 | 0)
                me.blocks[me.request_queue_ids[i]] = data[i];

            // Empty request queue
            me.request_queue = [];
            me.request_queue_ids = [];

            me.callback(0);
        }, true);
    };

    /** @type {RemoteHardDiskImage[]} */
    window["drives"] = [];

    var paused = 0,
        now, cycles, cyclebase = 0,
        run;
    $("pause").addEventListener("click", function () {
        paused ^= 1;
        if (paused) {
            $("pause").innerHTML = "Run";
        } else {
            $("pause").innerHTML = "Pause";
        }
        if (paused == 0) run_wrapper(0);
    });

    function run_wrapper2() {

        wrap("emscripten_init")();
        // Check if we should load a savestate
        var statepath = getParameterByName("state");
        if (statepath) {
            // Load all the files that we can
            loadFiles([
                statepath + "/state.bin",
                statepath + "/ram",
                statepath + "/vram",
                statepath + "/diskinfo.json"], function (err, data) {
                    if (err) throw err;
                    savestate_files["/state.bin"] = data[0];
                    savestate_files["/ram"] = data[1];
                    savestate_files["/vram"] = data[2];
                    savestate_files["/diskinfo.json"] = JSON.parse(u8tostr(data[3]));
                    
                    wrap("emscripten_load_state")();

                    delete data[3]; // try to get this gc'ed 

                    now = new Date().getTime();
                    cycles = wrap("emscripten_get_cycles");
                    run = wrap("emscripten_run");
                    run_wrapper();
                }, true);
                return;
        }

        now = new Date().getTime();
        cycles = wrap("emscripten_get_cycles");
        run = wrap("emscripten_run");
        run_wrapper();
    }

    function run_wrapper() {
        if (paused) return;
        try {
            //if(runs++ > 500) throw new Error("stop");
            var x = run();
            //console.log("Pausing ", x, "ms");
            var temp;
            var elapsed = (temp = new Date().getTime()) - now;
            if (elapsed >= 1000) {
                var curcycles = cycles();
                $("speed").innerHTML = ((curcycles - cyclebase) / (elapsed) / (1000)).toFixed(2);
                cyclebase = curcycles;
                now = temp;
            }
            //x = 0;
            //update_screen();
            setTimeout(run_wrapper, x);
        } catch (e) {
            $("error").innerHTML = "Exception thrown -- see JavaScript console";
            $("messages").innerHTML = e.toString() + "<br />" + e.stack;
            failed = true;
            console.log(e);
            throw e;
        }
    }

    var requests_in_progress = 0;
    window["load_file_xhr"] = function (lenptr, dataptr, path) {
        var src = readstr(path);
        requests_in_progress++;
        loadFiles([src], function (err, data) {
            if (err) throw err;
            data = data[0];

            var destination = Module["_emscripten_alloc"](data.length, 4096);
            memcpy(destination, data);
            i32[lenptr >> 2] = data.length;
            i32[dataptr >> 2] = destination;
            requests_in_progress--;
            if (requests_in_progress === 0) run_wrapper2();
        });
    };
    window["drive_init"] = function (info_ptr, path, id) {
        var p = readstr(path);
        requests_in_progress++;
        loadFiles([join_path(p, "info.dat")], function (err, data) {
            if (err) throw err;
            data = data[0];
            var rhd = new RemoteHardDiskImage(id, p, data);
            var dataptr = alloc(data.length), strptr = alloc(p.length + 1);
            memcpy(dataptr, data);
            strcpy(strptr, p);
            wrap("drive_emscripten_init")(info_ptr, strptr, dataptr, id);
            gc();

            // Register it with Emscripten
            window["drives"][id] = rhd;
            requests_in_progress--;
            if (requests_in_progress === 0) run_wrapper2();
        });
    };

    var cfg = buildConfiguration();
    loadEmulator(getParameterByName("app") || "halfix.js");

    var savestate_files = {};
    function u8tostr(u) {
        var str = "";
        for (var i = 0; i < u.length; i = i + 1 | 0)str += String.fromCharCode(u[i]);
        return str;
    }

    Module["onRuntimeInitialized"] = function () {
        // Initialize runtime
        u8 = Module["HEAPU8"];
        u16 = Module["HEAPU16"];
        i32 = Module["HEAP32"];

        // Load configuration file into Halfix
        var pc = Module["_emscripten_get_pc_config"]();

        var area = alloc(cfg.length + 1);
        strcpy(area, cfg);
        Module["_parse_cfg"](pc, area);

        var fast = getIntegerByName("fast", 0);
        Module["_emscripten_set_fast"](fast);

        gc();
    };

    // Various convienience functions
    $("ctrlaltdel").addEventListener("mousedown", function () {
        wrap("display_send_ctrl_alt_del")(1);
    });
    $("ctrlaltdel").addEventListener("mouseup", function () {
        wrap("display_send_ctrl_alt_del")(0);
    });

    var jszip;
    $("savebutton").addEventListener("click", function () {
        jszip = new JSZip();
        wrap("emscripten_savestate")();
        jszip.generateAsync({ type: "blob" })
            .then(function (content) {
                alert("todo: save content -- zipping is too slow");
                //saveAs(content, "example.zip");
            });
    });
    window["saveFile"] = function (pathstr, fileidx, len) {
        var path = readstr(pathstr);
        jszip.file(path, Module["HEAPU8"].slice(fileidx, fileidx + len | 0));
    };

    /**
     * Load file from file cache
     * @param {number} pathstr Pointer to path string
     * @param {number} addr Address to load the data
     */
    window["loadFile"] = function (pathstr, addr) {
        var path = readstr(pathstr);
        var data = savestate_files[path];
        if(!data) throw new Error("ENOENT: " + path);
        memcpy(addr, data);
        return addr;
    };
    /**
     * Load file from file cache and allocate a buffer to store it in. 
     * It is the responsibility of the caller to free the memory. 
     * @param {number} pathstr Pointer to path string
     * @param {number} addr Address to load the data
     */
    window["loadFile2"] = function (pathstr, addr) {
        var path = readstr(pathstr);
        var data = savestate_files[path];
        if(!data) throw new Error("ENOENT: " + path);
        var len = data.length;
        var addr = alloc(len);
        _allocs.pop();
        memcpy(addr, data);
        console.log(path, data, addr);
        return addr;
    };
})();