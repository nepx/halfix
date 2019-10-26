// Halfix emulator loader and minimal runtime initializer.
// Copyright 2019 Joey Kim. All rights reserved..

// Some URL query parameters you may find useful:
//  ?app=file.js  -  Load Halfix from path "file.js" (default: halfix.js)
//  ?hd[a|b|c|d]=imgdir  -  Set hard disk image (none by default)
//  ?pcienabled=true  -  Enable PCI support (disabled by default)
//  ?apicenabled=true  -  Enable APIC support (disabled by default)
//  ?bios=file.bin  -  Load BIOS image from "file.bin" (default: bios.bin)
//  ?vgabios=file.bin  - Load VGA BIOS image from "file.bin" (default: vgabios.bin)
//  ?now=time  - Set emulator time seen in emulator (default: 29 July 2019)
//  ?mem=32  - Set size of emulated RAM (default: 32 MB)

function $(e) {
    return document.getElementById(e);
}

var printElt = $("log"),
    netstat = $("netstat"),
    totalbytesElt = $("totalbytes");
var Module = {
    "canvas": $("screen"),
    "print": function (ln) {
        printElt.value += ln + "\n";
    }
};

(function () {
    // ============================================================================
    // API functions
    // ============================================================================
    var image_data = null,
        ctx = Module["canvas"].getContext("2d");
    window["update_size"] = function (fbptr, x, y) {
        if ((x & y) == 0) return; // Don't do anything if x or y is zero (VGA resizing sometimes gives weird sizes)
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
        return normalize_path(a + b);
    }

    // Emscripten heap views
    /** @type {Uint8Array} */
    var u8,
        /** @type {Uint16Array} */
        u16,
        /** @type {Int32Array} */
        i32;

    // ============================================================================
    // Emscripten helper functions
    // ============================================================================
    var options = {
        bios_path: getParameterByName("bios") || "bios.bin",
        bios: null,
        vgabios_path: getParameterByName("vgabios") || "vgabios.bin",
        vgabios: null,
        hd: [getParameterByName("hda"), getParameterByName("hdb"), getParameterByName("hdc"), getParameterByName("hdd")],
        pci: getParameterByName("pcienabled") === "true",
        apic: getParameterByName("apicenabled") === "true",
        now: getParameterByName("now") ? parseFloat(getParameterByName("now")) : 1563602400,
        mem: getParameterByName("mem") ? parseInt(getParameterByName("mem")) : 32
    };

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
    Module["TOTAL_MEMORY"] = roundUp(options.mem + 32 + 2) * 1024 * 1024 | 0;

    // Returns a pointer to an Emscripten-compiled function
    function wrap(nm) {
        return Module["_" + nm];
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
     * Copy a string into memory
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

    /**
     * Frees every single patch of memory we have reserved with alloc()
     */
    function gc() {
        var free = Module["_free"];
        for (var i = 0; i < _allocs.length; i = i + 1 | 0) free(_allocs[i]);
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

    Module["onRuntimeInitialized"] = function () {
        // Load ROMs into memory to be copied.
        var load_rom = wrap("emscripten_alloc");
        var bios = load_rom(1, options.bios.length);
        var vgabios = load_rom(0, options.vgabios.length);

        wrap("emscripten_set_time")(options.now);
        wrap("emscripten_set_memory_size")(options.mem * 1024 * 1024 | 0);

        u8 = Module["HEAPU8"];
        u16 = Module["HEAPU16"];
        i32 = Module["HEAP32"];


        u8.set(options.bios, bios);
        u8.set(options.vgabios, vgabios);

        for (var i = 0; i < 4; i = i + 1 | 0)
            if (options.hd[i])
                options.hd[i] = new RemoteHardDiskImage(i, options.hd[i].path, options.hd[i].info);

        // Start initializing disks
        wrap("emscripten_enable_pci")(options.pci);
        wrap("emscripten_enable_apic")(options.apic);
        console.log(options);

        wrap("emscripten_initialize")();
        window["debug"] = wrap("emscripten_debug");
        var run = wrap("emscripten_run"), cycles = wrap("emscripten_get_cycles"), cyclebase = 0;
        var n = 0,
            now = new Date().getTime();

        var start = new Date().getTime(),
            runs = 0,
            failed = false;

        var paused = 0;
        $("pause").addEventListener("click", function () {
            paused ^= 1;
            if (paused) {
                $("pause").innerHTML = "Run";
            } else {
                $("pause").innerHTML = "Pause";
            }
            if (paused == 0) run_wrapper(0);
        });

        var cycles_slept = 0;

        function run_wrapper() {
            if (failed) return;
            if (paused) return;
            try {
                //if(runs++ > 500) throw new Error("stop");
                var x = run();
                //console.log("Pausing ", x, "ms");
                n++;
                var temp;
                var elapsed = (temp = new Date().getTime()) - now;
                if (elapsed >= 1000) {
                    console.log("FPS: ", n);
                    var curcycles = cycles();
                    $("speed").innerHTML = ((curcycles - cyclebase) / (elapsed ) / (1000)).toFixed(2);
                    cyclebase = curcycles;
                    now = temp;
                    n = 0;
                }
                //x = 0;
                //update_screen();
                setTimeout(run_wrapper, x);
            } catch (e) {
                $("error").innerHTML = "Exception thrown -- see JavaScript console";
                $("messages").innerHTML = e.toString() + "<br />" + e.stack;
                failed = true;
                throw e;
            }
        }
        run_wrapper();
    };

    // ============================================================================
    // Drive helper functions
    // ============================================================================
    /**
     * Create a disk image and set up Emscripten state
     * @param {number} diskid 
     * @param {string} path 
     * @param {Uint8Array} info 
     * @constructor
     */
    function RemoteHardDiskImage(diskid, path, info) {
        // Call initialization mechanism
        var pathaddr = alloc(path.length + 1 | 0),
            infoaddr = alloc(info.length);
        strcpy(pathaddr, path);
        memcpy(infoaddr, info);
        // void emscripten_init_disk(int disk, int has_media, char* path, void* info);
        wrap("emscripten_init_disk")(diskid | 0, 1, pathaddr, infoaddr);
        gc();

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
        if (!this.blocks[id]){
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

        console.log(this.request_queue);

        loadFiles(this.request_queue, function (err, data) {
            if (err) throw err;

            var rql = me.request_queue.length;
            //console.log(me.request_queue_ids);
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

    // Load BIOS images and disk information, if required.
    loadFiles([options.bios_path, options.vgabios_path], function (err, data) {
        if (err) throw error;
        // Initialize disk drives, if required.
        var disk_drives = [],
            disk_drive_ids = [];
        for (var i = 0; i < 4; i++) {
            if (options.hd[i]) {
                disk_drives.push(join_path(options.hd[i], "info.dat"));
                disk_drive_ids.push(i);
            }
        }

        options.bios = data[0];
        options.vgabios = data[1];

        function ready() {
            loadEmulator(getParameterByName("app") || "halfix.js");
            // Execution continues from onRuntimeInitialized
        }
        if (disk_drives.length)
            loadFiles(disk_drives, function (err, data) {
                if (err) throw err;
                for (var i = 0; i < data.length; i++)
                    options.hd[i] = {
                        info: data[i],
                        path: disk_drives[i].replace("info.dat", "") // XXX
                    };
                ready();
            });
        else ready();
    });

    // ============================================================================
    // Savestate handlers
    // ============================================================================

    var zip = null;
    /**
     * Save a file to the JSZip instance. 
     * 
     * @param {number} nameptr Pointer to the name string
     * @param {number} ptr Pointer to the data
     * @param {number} length Length of the data
     */
    function saveFile(nameptr, ptr, length) {
        var name = normalize_path(readstr(nameptr)).substring(1); // Trim off leading /
        console.log(name);
        zip.file(name, u8.slice(ptr, ptr + length | 0));
    }
    window["saveFile"] = saveFile;
    /**
     * Stores the current Halfix state to a ZIP file. Uses the JSZip API
     */
    function saveState() {
        if (inLoading) {
            savestateRequested = true;
            return;
        }
        zip = new JSZip();
        wrap("emscripten_savestate")();
        console.log("HERE\n");
        zip.generateAsync({
            "type": "blob",
            "compression": "DEFLATE",
            "compressionOptions": {
                "level": 6
            }
        }).then(function (blob) {
            var a = document.createElement("a");
            a.href = URL.createObjectURL(blob);
            document.body.appendChild(a);
            a.click();
        });
    }

    $("savebutton").addEventListener("click", saveState);
})();