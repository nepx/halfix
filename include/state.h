#ifndef STATE_H
#define STATE_H

#include <stdint.h>

typedef void (*state_handler)(void);
void state_read_from_file(char* path);
void state_store_to_file(char* path);
void state_register(state_handler s);

#define TYPE_DATA 0
#define TYPE_OBJECT 1
struct bjson_data {
    uint32_t length;
    void* data;
};

struct bjson_key_value {
    char* key;
    uint8_t datatype;
    union {
        struct bjson_data mem_data;
        void* ptr_value;
    };
};
struct bjson_object {
    uint8_t length;
    struct bjson_key_value* keys;
};
struct bjson_array {
    uint32_t length; // Number of elements
    //uint8_t type; // Type of elements
    union {
        uint8_t* bytearray;
        char** strarray;
    };
};
struct bjson_string {
    uint32_t length;
    char* data;
};

void state_file(int size, char* name, void* ptr);
void state_array(int size, int ellen, char* name, void* ptr);
void state_integer(int size, char* name, void* ptr);
int state_is_reading(void);

int state_get_fd(char* path);
void state_close_fd(int fd);
void state_mkdir(char* dir);
void state_read(int fd, void* information, int bytelen);
void* state_readfile(char* data);
void state_freefile(void* information);

struct bjson_object* state_obj(char* name, int keyvalues);
void state_field(struct bjson_object*, int length, char* name, void*);
void state_string(struct bjson_object* cur, char* name, char** val);
char* state_get_path_base(void);

#define FIELD(y) state_field(obj, sizeof(y), #y, &y)


#ifdef EMSCRIPTEN
#define DISABLE_CONSTANT_SAVING
#undef SAVESTATE
#define DISABLE_RESTORE
#endif

#endif