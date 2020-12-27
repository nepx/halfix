(function (global) {
    /**
     * Emulator constructor
     * @param {object} options 
     */
    function Halfix(options) {
        this.options = options || {};

        this.canvas = this.options["canvas"] || null;
        this.ctx = this.canvas ? this.canvas.getContext("2d") : null;

        this.total_memory = 256;

        this.fast = options["fast"] || false;
        this.winnt_hack = options["winnt_hack"] || false;

        this.reportSpeed = options["reportSpeed"] || function (n) { };
        console.log(options.reportSpeed);

        this.paused = false;

        /** @type {ImageData} */
        this.image_data = null;

        this.config = this.buildConfiguration();

        this.onprogress = options["onprogress"] || function (a, b, c) { };
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
        if (res === null) return defaultValue | 0;
        else return !!res | 0;
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
     * @param {string} f name of file
     * @param {number} a current position
     * @param {number} b end
     */
    Halfix.prototype.updateNetworkProgress = function (f, a, b) {
        this.onprogress(f, a, b);
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

        this.total_memory = roundUp(mem + 32 + vgamem) * 1024 * 1024 | 0;
        //Module["INITIAL_MEMORY"] = roundUp(mem + 32 + vgamem) * 1024 * 1024 | 0;
        config.push("memory=" + mem + "M");
        config.push("vgamemory=" + vgamem + "M");
        this.buildDrive(config, "a", 0, "master");
        this.buildDrive(config, "b", 0, "slave");
        this.buildDrive(config, "c", 1, "master");
        this.buildDrive(config, "d", 1, "slave");

        this.buildFloppy(config, "a");
        this.buildFloppy(config, "b");
        config.push("[boot]");

        var bootOrder = this.getParameterByName("boot") || "hcf";
        config.push("a=" + bootOrder[0] + "d");
        config.push("b=" + bootOrder[1] + "d");
        config.push("c=" + bootOrder[2] + "d");

        config.push("[cpu]");
        config.push("cpuid_limit_winnt=" + (this.winnt_hack ? "1" : "0"));
        config.push(""); // Trailing empty line

        return config.join("\n");
    }
    Halfix.prototype["send_ctrlaltdel"] = function () {
        send_ctrlaltdel(1);
        send_ctrlaltdel(0);
    };

    function loadFiles(paths, cb, gz) {
        var resultCounter = paths.length | 0,
            results = [];

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
                        _halfix.updateNetworkProgress(path, e.loaded, e.total);
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

    Halfix.prototype.updateScreen = function () {
        if (this.ctx)
            this.ctx.putImageData(this.image_data, 0, 0);
    };

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

    var init_cb = null;

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
        //global["Module"]["INITIAL_MEMORY"] = this.total_memory;
        //global["Module"]["MEMORY_SIZE"] = this.total_memory;

        init_cb = cb;

        // Load emulator
        var script = document.createElement("script");
        script.src = this.getParameterByName("emulator") || "halfix.js";
        document.head.appendChild(script);
    };

    var savestate_files = {};
    function u8tostr(u) {
        var str = "";
        for (var i = 0; i < u.length; i = i + 1 | 0)str += String.fromCharCode(u[i]);
        return str;
    }
    /**
     * Load savestate from directory
     * @param {string} statepath
     * @param {function} cb
     */
    Halfix.prototype["loadStateXHR"] = function (statepath, cb) {
        var me = this;
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

                cb();
            }, true);
    };

    /**
     * Pause the emulator
     * @param {boolean} paused
     */
    Halfix.prototype["pause"] = function (paused) {
        this.paused = paused;
    };

    /**
     * Send a fullscreen request to the brower. 
     */
    Halfix.prototype["fullscreen"] = function () {
        Module["requestFullscreen"]();
    };
    var cyclebase = 0;

    function run_again(me, x) {
        if (requests_in_progress !== 0) {
            setTimeout(function () { run_again(me, x); }, x)
        } else
            me["run"]();
    }

    Halfix.prototype["run"] = function () {
        if (this.paused) return;
        try {
            var x = run();
            var temp;
            var elapsed = (temp = new Date().getTime()) - now;
            if (elapsed >= 1000) {
                var curcycles = cycles();
                this.reportSpeed(((curcycles - cyclebase) / (elapsed) / (1000)).toFixed(2));
                console.log(((curcycles - cyclebase) / (elapsed) / (1000)).toFixed(2));
                //$("speed").innerHTML = ((curcycles - cyclebase) / (elapsed) / (1000)).toFixed(2);
                cyclebase = curcycles;
                now = temp;
            }
            var me = this;
            setTimeout(function () {
                run_again(me, x);
            }, x);
        } catch (e) {
            $("error").innerHTML = "Exception thrown -- see JavaScript console";
            $("messages").innerHTML = e.toString() + "<br />" + e.stack;
            failed = true;
            console.log(e);
            throw e;
        }
    };

    // ========================================================================
    // Emscripten support code
    // ========================================================================

    var dynCall_vii, send_ctrlaltdel;
    var cycles, now;
    function run_wrapper2() {
        wrap("emscripten_init")();

        now = new Date().getTime();
        cycles = wrap("emscripten_get_cycles");
        get_now = wrap("emscripten_get_now");
        dynCall_vii = wrap("emscripten_dyncall_vii");
        run = wrap("emscripten_run");
        send_ctrlaltdel = wrap("display_send_ctrl_alt_del");

        wrap("emscripten_set_fast")(_halfix.fast);
        init_cb();
    }

    // Initialize module instance
    global["Module"] = {};

    // Set up some stuff that we might find helpful

    const SAVE_LOGS = true;
    var arr = [];
    Module["printErr"] = function (ln) { if (SAVE_LOGS) arr.push(ln); };
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
    window["saveLog"] = saveLog;
    Module["print"] = function (ln) { console.log(ln); };

    global["update_screen"] = function () {
        _halfix.updateScreen();
    };

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
            var driver = new xhr_replacements[pathparts[0]](data);
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
        image.init(p, function (err, data) {
            if (err) throw err;

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
        /** @type {HardDriveImage} */
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
        return parseInt(parts[1], 16) >>> 0;
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
            var blockoffs = (_url_to_blkid(i) << 18) >>> 0;
            data[i] = this.data.slice(blockoffs, (blockoffs + (256 << 10)) >>> 0);
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
        console.log(reqs);
        // Ensure that loads are in order and consecutive, which speeds things up
        var blockBase = _url_to_blkid(reqs[0]);
        for (var i = 1; i < reqs.length; i = i + 1 | 0)
            if ((_url_to_blkid(reqs[i]) - i | 0) !== blockBase) throw new Error("non-consecutive reads");
        var blocks = reqs.length;

        /** @type {File} */
        var fileslice = this.file.slice((blockBase << 18) >>> 0, ((blockBase + blocks) << 18) >>> 0);

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
        fr.readAsArrayBuffer(fileslice);
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
        loadFiles(reqs, cb, true);
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
        dynCall_vii(cb, cb_ptr, arg2);
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

    // Some more savestate-related functions
    /**
     * Load file from file cache
     * @param {number} pathstr Pointer to path string
     * @param {number} addr Address to load the data
     */
    window["loadFile"] = function (pathstr, addr) {
        var path = readstr(pathstr);
        var data = savestate_files[path];
        if (!data) throw new Error("ENOENT: " + path);
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
        if (!data) throw new Error("ENOENT: " + path);
        var len = data.length;
        var addr = alloc(len);
        _allocs.pop();
        memcpy(addr, data);
        console.log(path, data, addr);
        return addr;
    };

    if (typeof module !== "undefined" && module["exports"])
        module["exports"] = Halfix;
    else
        global["Halfix"] = Halfix;
})(typeof window !== "undefined" ? window : global);
