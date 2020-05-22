(function (global) {
    /**
     * Emulator constructor
     * @param {object} options 
     */
    function Halfix(options) {
        this.options = options || {};

        this.config = this.buildConfiguration();
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
    Halfix.prototype.updateNetworkProgress = function(progress){
        
    };
    /**
     * @param {number} total Total bytes loaded
     */
    Halfix.prototype.updateTotalBytes = function(total){
        
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

        // Load emulator
        var script = document.createElement("script");
        script.src = this.getParameterByName("emulator") || "halfix.js";
        document.head.appendChild(script);
    };

    // ========================================================================
    // Emscripten support code
    // ========================================================================

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
        var cb = function(err, data){
            if(err) throw err;
            
            var destination = Module["_emscripten_alloc"](data.length, 4096);
            memcpy(destination, data);

            i32[lenptr >> 2] = data.length;
            i32[dataptr >> 2] = destination;
            requests_in_progress = requests_in_progress + 1 | 0;

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
        }else loadFiles([pathstr], function(err, datas){
            cb(err, datas[0]);
        }, false);

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

        gc();
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
        "fr": FileReaderLoader,
        "ab": ArrayBufferLoader
    }

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

    global["Halfix"] = Halfix;
})(typeof window !== "undefined" ? window : this);