(function (global) {
    /**
     * Emulator constructor
     * @param {object} options 
     */
    function Halfix(options) {
        this.options = options || {};

        this.canvas = this.options["canvas"] || null;

        this.config = this.buildConfiguration();

        /** @type {ImageData} */
        this.image_data = null;
    }

    var _cache = [];

    /**
     * @param {string} name
     * @returns {string|null} Value of the parameter or null
     */
    Halfix.prototype.getParameterByName = function (name) {
        var opt = this.options[name];
        if (!opt) return null;

        // Check if we have a special kind of object here
        var index = _cache.length;
        if (opt instanceof ArrayBuffer) {
            _cache.push(opt);
            // Return that we have this in the cache 
            return "ab!" + index;
        } else if (opt instanceof File) {
            _cache.push(opt);
            return "file!" + index;
        }
        return opt;
    };

    /**
     * @param {string} name
     * @param {boolean} defaultValue
     * @returns {boolean} Value of parameter as a boolean
     */
    Halfix.prototype.getBooleanByName = function (name, defaultValue) {
        var res = this.getParameterByName(name);
        if (res === null) return defaultValue;
        else return !!res;
    };
    /**
     * @param {string} name
     * @param {number} defaultValue
     * @returns {number} Value of parameter as an integer
     */
    Halfix.prototype.getIntegerByName = function (name, defaultValue) {
        var res = this.getParameterByName(name);
        if (res === null) return defaultValue;
        else return parseInt(res) | 0;
    };
    /**
     * @param {string} name
     * @param {number} defaultValue
     * @returns {number} Value of parameter as a double
     */
    Halfix.prototype.getDoubleByName = function (name, defaultValue) {
        var res = this.getParameterByName(name);
        if (res === null) return defaultValue;
        else return +parseFloat(res);
    };

    /**
     * Build drive configuration
     * @param {array} config
     * @param {string} drvid A drive ID (a/b/c/d)
     * @param {number} primary Primary? (0=yes, 1=no)
     * @param {number} master Master? (0=yes, 1=no)
     */
    Halfix.prototype.buildDrive = function (config, drvid, primary, master) {
        var hd = this.getParameterByName("hd" + drvid),
            cd = this.getParameterByName("cd" + drvid);
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
    };

    /**
     * @param {number} progress Fraction of completion * 100 
     */
    Halfix.prototype.updateNetworkProgress = function (progress) {

    };
    /**
     * @param {number} total Total bytes loaded
     */
    Halfix.prototype.updateTotalBytes = function (total) {

    };

    Halfix.prototype.handleSavestate = function () {
        // TODO
    };

    /**
     * Build floppy configuration
     */
    Halfix.prototype.buildFloppy = function (config, drvid) {
        var fd = this.getParameterByName("fd" + drvid);
        if (!fd)
            return;
        config.push("[fd" + drvid + "]");
        config.push("file=" + fd);
        config.push("inserted=1");
    }

    /**
     * From the given URL parameters, we build a .conf file for Halfix to consume. 
     */
    Halfix.prototype.buildConfiguration = function () {
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
        config.push("bios=" + (this.getParameterByName("bios") || "bios.bin"));
        config.push("vgabios=" + (this.getParameterByName("vgabios") || "vgabios.bin"));
        config.push("pci=" + this.getBooleanByName("pcienabled", true));
        config.push("apic=" + this.getBooleanByName("apicenabled", true));
        config.push("acpi=" + this.getBooleanByName("acpienabled", true));
        config.push("pcivga=" + this.getBooleanByName("pcivga", false));
        config.push("now=" + this.getDoubleByName("now", new Date().getTime()));
        var floppyRequired = (!!this.getParameterByName("fda") || !!this.getParameterByName("fdb")) | 0;
        console.log(floppyRequired);
        config.push("floppy=" + floppyRequired);
        var mem = this.getIntegerByName("mem", 32),
            vgamem = this.getIntegerByName("vgamem", 4);

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
        this.buildDrive(config, "a", 0, "master");
        this.buildDrive(config, "b", 0, "slave");
        this.buildDrive(config, "c", 1, "master");
        this.buildDrive(config, "d", 1, "slave");

        this.buildFloppy(config, "a");
        this.buildFloppy(config, "b");
        config.push("[boot]");

        var bootOrder = this.getParameterByName("boot") || "chf";
        config.push("a=" + bootOrder[0] + "d");
        config.push("b=" + bootOrder[1] + "d");
        config.push("c=" + bootOrder[2] + "d");

        return config.join("\n");
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
                        _halfix.updateNetworkProgress(now - lastProgress | 0);
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
                    _halfix.updateTotalBytes(xhr.response.byteLength | 0);
                    if (resultCounter === 0) {
                        cb(null, results);

                        inLoading = false;

                        // If we have requested a savestate, then create it now.
                        _halfix.handleSavestate();
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

    var _loaded = false;

    // ========================================================================
    // Public API
    // ========================================================================
    /**
     * Get configuration file
     * @returns {string} Configuration text
     */
    Halfix.prototype["getConfiguration"] = function () {
        return this.config;
    };

    /** @type {Halfix} */
    var _halfix = null;

    /**
     * Load and initialize emulator instance
     * @param {function(Error, object)} cb Callback
     */
    Halfix.prototype["init"] = function (cb) {
        // Only one instance can be loaded at a time, unfortunately
        if (_loaded) cb(new Error("Already initialized"), null);
        _loaded = true;

        // Save our Halfix instance for later
        _halfix = this;

        // Set up our module instance
        global["Module"]["canvas"] = this.canvas;

        // Load emulator
        var script = document.createElement("script");
        script.src = this.getParameterByName("emulator") || "halfix.js";
        document.head.appendChild(script);
    };

    // ========================================================================
    // Emscripten support code
    // ========================================================================

    function run_wrapper2() {
        wrap("emscripten_init")();
    }

    // Initialize module instance
    global["Module"] = {};

    var requests_in_progress = 0;
    /**
     * @param {number} lenptr Pointer to length (type: int*)
     * @param {number} dataptr Pointer to allocated data (type: void**)
     * @param {number} Pointer to path (type: char*)
     */
    global["load_file_xhr"] = function (lenptr, dataptr, path) {
        var pathstr = readstr(path);
        var cb = function (err, data) {
            if (err) throw err;

            var destination = Module["_emscripten_alloc"](data.length, 4096);
            memcpy(destination, data);

            i32[lenptr >> 2] = data.length;
            i32[dataptr >> 2] = destination;
            requests_in_progress = requests_in_progress - 1 | 0;

            if (requests_in_progress === 0) run_wrapper2();
        };

        // Increment requests in progress by one
        requests_in_progress = requests_in_progress + 1 | 0;

        if (pathstr.indexOf("!") !== -1) {
            // If the user specified an arraybuffer or a file
            var pathparts = pathstr.split("!");
            var data = _cache[parseInt(pathparts[1])];
            /** @type {WholeFileLoader} */
            var driver = xhr_replacements[pathparts[0]](data);
            driver.load(cb);
        } else loadFiles([pathstr], function (err, datas) {
            cb(err, datas[0]);
        }, false);

    };

    /**
     * @param {number} fbptr Pointer to framebuffer information
     * @param {number} x The width of the window
     * @param {number} y The height of the window
     */
    global["update_size"] = function (fbptr, x, y) {
        if (x == 0 || y == 0) return; // Don't do anything if x or y is zero (VGA resizing sometimes gives weird sizes)
        Module["canvas"].width = x;
        Module["canvas"].height = y;
        _halfix.image_data = new ImageData(new Uint8ClampedArray(Module["HEAPU8"].buffer, fbptr, (x * y) << 2), x, y);
    };

    Module["onRuntimeInitialized"] = function () {
        // Initialize runtime
        u8 = Module["HEAPU8"];
        u16 = Module["HEAPU16"];
        i32 = Module["HEAP32"];

        // Get pointer to configuration
        var pc = Module["_emscripten_get_pc_config"]();

        var cfg = _halfix.getConfiguration();

        var area = alloc(cfg.length + 1);
        strcpy(area, cfg);
        Module["_parse_cfg"](pc, area);

        var fast = _halfix.getBooleanByName("fast", false);
        Module["_emscripten_set_fast"](fast);

        // The story continues in global["load_file_xhr"], which loads the BIOS/VGA BIOS files

        // Run some primitive garbage collection
        gc();
    };

    global["drives"] = [];

    global["drive_init"] = function (info_ptr, path, id) {
        var p = readstr(path), image;
        if (p.indexOf("!") !== -1) {
            var chunks = p.split("!");
            image = new image_backends[chunks[0]](_cache[parseInt(chunks[1]) | 0]);
        } else 
            image = new XHRImage();
        requests_in_progress = requests_in_progress + 1 | 0;
        image.init(p, function(err, data){
            if(err) throw err;

            var dataptr = alloc(data.length), strptr = alloc(p.length + 1);
            memcpy(dataptr, data);
            strcpy(strptr, p);
            wrap("drive_emscripten_init")(info_ptr, strptr, dataptr, id);
            gc();

            global["drives"][id] = image;
            requests_in_progress = requests_in_progress - 1 | 0;
            if (requests_in_progress === 0) run_wrapper2();
        });
    };

    // ========================================================================
    // Data reading functions
    // ========================================================================
    /**
     * @constructor
     */
    function WholeFileLoader() { }
    /**
     * Load a file 
     * @param {function} cb Callback
     */
    WholeFileLoader.prototype.load = function (cb) {
        throw new Error("requires implementation");
    };
    /**
     * @extends WholeFileLoader
     * @constructor
     * @param {ArrayBuffer} data
     */
    function ArrayBufferLoader(data) {
        this.data = data;
    }
    ArrayBufferLoader.prototype = new WholeFileLoader();
    ArrayBufferLoader.prototype.load = function (cb) {
        cb(null, new Uint8Array(this.data));
    };

    /**
     * @extends WholeFileLoader
     * @constructor
     * @param {File} file
     */
    function FileReaderLoader(file) {
        this.file = file;
    }
    FileReaderLoader.prototype = new WholeFileLoader();
    FileReaderLoader.prototype.load = function (cb) {
        var fr = new FileReader();
        fr.onload = function () {
            cb(null, new Uint8Array(fr.result));
        };
        fr.onerror = function (e) {
            cb(e, null);
        };
        fr.onabort = function () {
            cb(new Error("filereader aborted"), null);
        };
        fr.readAsArrayBuffer(this.file);
    };
    // see load_file_xhr
    var xhr_replacements = {
        "file": FileReaderLoader,
        "ab": ArrayBufferLoader
    };

    /**
     * Generic hard drive image. All you have to do is fill in 
     * @constructor
     */
    function HardDriveImage() {
        /** @type {Uint8Array[]} */
        this.blocks = [];

        /** @type {string[]} */
        this.request_queue = [];

        /** @type {number[]} */
        this.request_queue_ids = [];

        /** @type {number} */
        this.cb = 0;
        /** @type {number} */
        this.arg1 = 0;
    }
    /**
     * Adds a block to the cache. Called on IDE writes, typically
     * 
     * @param {number} id
     * @param {number} offset Offset in memory to read from
     * @param {number} length 
     */
    HardDriveImage.prototype["addCache"] = function (id, offset, length) {
        this.blocks[id] = u8.slice(offset, length + offset | 0);
    };
    /**
     * Reads a section of a block from the cache
     * 
     * See src/drive.c: drive_read_block_internal
     * 
     * @param {number} id Block ID
     * @param {number} buffer Position in the buffer
     * @param {number} offset Offset in the block to read from
     * @param {number} length Number of bytes to read
     */
    HardDriveImage.prototype["readCache"] = function (id, buffer, offset, length) {
        id = id | 0;
        if (!this.blocks[id]) {
            //printElt.value += "[JSError] readCache(id=0x" + id.toString(16) + ", buffer=0x" + buffer.toString(16) + ", length=0x" + buffer.toString(16) + ")\n";
            return 1; // No block here with that data.
        }
        var buf = this.blocks[id].subarray(offset, length + offset | 0);
        if (buf.length > length) throw new Error("Block too long");
        u8.set(this.blocks[id].subarray(offset, length + offset | 0), buffer);
        return 0;
    };
    /**
     * Writes a section of a block with some data
     * 
     * See src/drive.c: drive_write_block_internal
     * 
     * @param {number} id Block ID
     * @param {number} buffer Position in the buffer
     * @param {number} offset Offset in the block to read from
     * @param {number} length Number of bytes to read
     */
    HardDriveImage.prototype["writeCache"] = function (id, buffer, offset, length) {
        id = id | 0;
        if (!this.blocks[id]) {
            //printElt.value += "[JSError] writeCache(id=0x" + id.toString(16) + ", buffer=0x" + buffer.toString(16) + ", length=0x" + buffer.toString(16) + ")\n";
            return 1; // No block here with that data.
        }
        var buf = u8.subarray(buffer, length + buffer | 0);
        if (buf.length > length) throw new Error("Block too long");
        this.blocks[id].set(buf, offset);
        return 0;
    };
    /**
     * Queues a memory read. Useful for multi-block reads
     * 
     * @param {number} str Pointer to URL
     * @param {number} id Block ID to store it in
     */
    HardDriveImage.prototype["readQueue"] = function (str, id) {
        this.request_queue.push(readstr(str));
        this.request_queue_ids.push(id);
    };

    /**
     * Runs all requests simultaneously.
     * 
     * @param {number} cb Callback pointer
     * @param {number} arg1 Callback argument
     */
    HardDriveImage.prototype["flushReadQueue"] = function (cb, arg1) {
        /** @type {RemoteHardDiskImage} */
        var me = this;

        this.cb = cb;
        this.arg1 = arg1;

        this.load(this.request_queue, function (err, data) {
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
    /**
     * Call an Emscripten callback
     * @type {number} res
     */
    HardDriveImage.prototype.callback = function (res) {
        fptr_vii(this.cb | 0, this.arg1 | 0, res | 0);
    };
    /**
     * @param {string[]} paths
     * @param {function(object,Uint8Array[])} cb
     * 
     * Note that all Uint8Arrays passed back to cb MUST be BLOCK_SIZE bytes long
     */
    HardDriveImage.prototype.load = function (reqs, cb) {
        throw new Error("implement me");
    };

    /**
     * Initialize hard drive image
     * @param {string} arg
     * @param {function(Uint8Array)} cb
     */
    HardDriveImage.prototype.init = function (arg, cb) {
        throw new Error("implement me");
    };

    /**
     * Convert a URL (i.e. os2/blk0000005a.bin) into a number (i.e. 0x5a)
     * @param {string} str 
     * @return {number}
     */
    function _url_to_blkid(str) {
        var parts = str.match(/blk([0-9a-f]{8})\.bin/);
        return parseInt(parts[1], 16) | 0;
    }

    /**
     * Create an "info.dat" file
     * @param {number} size 
     * @param {number} blksize 
     * @returns {Uint8Array} The data that would have been contained in info.dat
     */
    function _construct_info(size, blksize) {
        var i32 = new Int32Array(2);
        i32[0] = size;
        i32[1] = blksize;
        return new Uint8Array(i32.buffer);
    }

    /**
     * ArrayBuffer-backed image
     * @param {ArrayBuffer} ab 
     * @constructor
     * @extends HardDriveImage
     */
    function ArrayBufferImage(ab) {
        this.data = new Uint8Array(ab);
    }
    ArrayBufferImage.prototype = new HardDriveImage();
    /**
     * @param {string[]} paths
     * @param {function(object,Uint8Array[])} cb
     */
    ArrayBufferImage.prototype.load = function (reqs, cb) {
        var data = [];
        for (var i = 0; i < reqs.length; i = i + 1 | 0) {
            // note to self: Math.log(256*1024)/Math.log(2) === 18
            var blockoffs = _url_to_blkid(i) << 18;
            data[i] = this.data.slice(blockoffs, blockoffs + (256 << 10) | 0);
        }
        setTimeout(function () {
            cb(null, data);
        }, 0);
    };
    ArrayBufferImage.prototype.init = function (arg, cb) {
        var data = _construct_info(this.data.byteLength, 256 << 10);
        setTimeout(function () {
            cb(null, data);
        }, 0);
    };

    /**
     * File API-backed image
     * @param {File} f 
     * @constructor
     * @extends HardDriveImage
     */
    function FileImage(f) {
        this.file = f;
    }
    FileImage.prototype = new HardDriveImage();
    FileImage.prototype.load = function (reqs, cb) {
        // Ensure that loads are in order and consecutive, which speeds things up
        var blockBase = _url_to_blkid(reqs[0]);
        for (var i = 1; i < reqs.length; i = i + 1 | 0)
            if ((_url_to_blkid(reqs[i]) - i | 0) !== blockBase) throw new Error("non-consecutive reads");
        var blocks = reqs.length;

        /** @type {File} */
        var fileslice = this.file.slice(blockBase << 18, (blockBase + blocks) << 18);

        var fr = new FileReader();
        fr.onload = function () {
            var arr = [];
            for (var i = 0; i < reqs.length; i = i + 1 | 0) {
                // Slice a 256 KB chunk of the file
                arr.push(new Uint8Array(fr.result.slice(i << 18, (i + 1) << 18)));
            }
            cb(null, arr);
        };
        fr.onerror = function (e) {
            cb(e, null);
        };
        fr.onabort = function () {
            cb(new Error("filereader aborted"), null);
        };
        fr.readAsArrayBuffer(this.file);
    };
    FileImage.prototype.init = function (arg, cb) {
        var data = _construct_info(this.file.size, 256 << 10);
        setTimeout(function () {
            cb(null, data);
        }, 0);
    };

    /**
     * XHR-backed image
     * @constructor
     * @extends HardDriveImage
     */
    function XHRImage() {
    }
    XHRImage.prototype = new HardDriveImage();
    XHRImage.prototype.load = function (reqs, cb) {
        loadFiles(reqs, cb);
    };
    XHRImage.prototype.init = function (arg, cb) {
        loadFiles([join_path(arg, "info.dat")], function (err, data) {
            if (err) throw err;
            cb(null, data[0]);
        });
    };

    var image_backends = {
        "file": FileImage,
        "ab": ArrayBufferImage
    };

    // ========================================================================
    // Useful functions
    // ========================================================================

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

    var u8 = null, u16 = null, i32 = null;
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

    global["Halfix"] = Halfix;
})(typeof window !== "undefined" ? window : this);