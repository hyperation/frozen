#ifndef BACKEND_H
#define BACKEND_H

enum request_actions {
	ACTION_CRWD_CREATE,
	ACTION_CRWD_READ,
	ACTION_CRWD_WRITE,
	ACTION_CRWD_DELETE,
	ACTION_CRWD_MOVE,
	ACTION_CRWD_COUNT,
	ACTION_CRWD_CUSTOM,
	
	REQUEST_INVALID = -1
};

/* PRIVATE - chains {{{1 */

typedef struct chain_t    chain_t;

typedef int (*f_init)      (chain_t *);
typedef int (*f_configure) (chain_t *, hash_t *);
typedef int (*f_destroy)   (chain_t *);

typedef enum chain_types {
	CHAIN_TYPE_CRWD
	
} chain_types;

/* chain type crwd */

typedef ssize_t (*f_crwd) (chain_t *, request_t *);

struct chain_t {
	char *      name;
	chain_types type;
	
	f_init      func_init;
	f_configure func_configure;
	f_destroy   func_destroy;
	union {
		struct {
			f_crwd  func_create;
			f_crwd  func_set;
			f_crwd  func_get;
			f_crwd  func_delete;
			f_crwd  func_move;
			f_crwd  func_count;
			f_crwd  func_custom;
		} chain_type_crwd;
	};
	
	chain_t *   next;
	void *      user_data;
}; 

#define CHAIN_REGISTER(chain_info) \
	static void __attribute__((constructor))__chain_init_##chain_info() \
	{ \
		_chain_register(&chain_info); \
	}

void        _chain_register    (chain_t *chain);
chain_t *   chain_new          (char *name);
ssize_t     chain_query        (chain_t *chain, request_t *request);
ssize_t     chain_next_query   (chain_t *chain, request_t *request);
void        chain_destroy      (chain_t *chain);
_inline int chain_configure    (chain_t *chain, hash_t *config);

/* }}}1 */

/* backends {{{ */

typedef struct backend_t {
	char           *name;
	chain_t        *chain;
	unsigned int    refs;
} backend_t;

API backend_t *     backend_new             (hash_t *config);
API ssize_t         backend_query           (backend_t *backend, request_t *request);
API void            backend_destroy         (backend_t *backend);

API void            backend_buffer_io_init  (buffer_t *buffer, chain_t *chain, int cached);
API buffer_t *      backend_buffer_io_alloc (chain_t *chain, int cached);

/* }}} */

request_actions     request_str_to_action   (char *string);

#endif // BACKEND_H