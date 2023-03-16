
struct implementation_t {
    void (*close)(struct implementation_t*);
    void (*free)(struct implementation_t*);
    int (*open)(struct implementation_t*);
    int (*size)(struct implementation_t*);
    int (*add)(struct implementation_t*, char* s, char* p, char* o, char* c);
    int (*remove)(struct implementation_t*, char* s, char* p, char* o, char* c);
    int (*contains)(struct implementation_t*, char* s, char* p, char* o,
		    char* c);
    struct implementation_stream_t* (*new_stream)(struct implementation_t *,
						  char*, char*, char*, char*);
    void* store;
};

typedef struct implementation_t implementation;

struct implementation_stream_t {
    implementation* impl;
    void (*free)(struct implementation_stream_t*);
    int (*get_s)(struct implementation_stream_t*, const char**, size_t*);
    int (*get_p)(struct implementation_stream_t*, const char**, size_t*);
    int (*get_o)(struct implementation_stream_t*, const char**, size_t*);
    int (*at_end)(struct implementation_stream_t*);
    int (*next)(struct implementation_stream_t*);
    void* stream;
};

typedef struct implementation_stream_t implementation_stream;

extern implementation* implementation_new(char* name, int sync, int is_new);

