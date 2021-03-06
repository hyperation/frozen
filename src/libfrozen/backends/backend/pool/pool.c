#include <libfrozen.h>
#include <backend.h>
#include <pthread.h>

/**
 * @ingroup backend
 * @addtogroup mod_pool backend/pool
 *
 * Pool module inserted in forkable backend chain can track any property of backend (like memory usage)
 * and limit it. Underlying modules can be destroyed or call special function.
 *
 */
/**
 * @ingroup mod_pool
 * @page page_pool_config Configuration
 * 
 * Accepted configuration:
 * @code
 * 	{
 * 	        class             = "pool",
 * 	        parameter         =                    # Step 1: how <parameter> value will be obtained:
 * 	                            "ticks",           #  - <parameter> is ticks collected in last <ticks_interval> seconds, default
 * 	                            "request",         #  - send <parameter_request> + HK(buffer) -> (uint_t).
 * 	                            "one",             #  - <parameter> == 1 if pool not killed
 *
 *                                                     # Step 2: limit
 *              max_perinstance   = (uint_t)'1000',    # set maximum <parameter> value for one instance to 1000
 *              max_perfork       = (uint_t)'2000',    # set maximum <parameter> value for group of forked backends to 2000
 *              max_global        = (uint_t)'3000',    # set maximum <parameter> value for all backends (can be different for each instance)
 *
 *              mode_perfork      =                    # Step 3: choose victum
 *                                  "first",           #   - earliest registered backend, default
 *                                  "last",            #   - latest registered backend
 *                                  "random",          #   - random item
 *                                  "highest",         #   - backend with higest <parameter> value
 *                                  "lowest",          #   - backend with lowest <parameter> value
 *              mode_global       ... same as _perfork #
 *                                                     
 *                                                     # Step 4: what to do with victum?
 *              action            =                    # what to do with choosed module:
 *                                  "request"          #   - send request <action_request_one>, default
 *              action_one        ... same as action   # override for _one action
 *              action_perfork    ... same as action   # override for _perfork action
 *              action_global     ... same as action   # override for _global action
 *              
 *              parameter_request      = { ... }       #
 *              action_request         = { ... }       # this is default action
 *              action_request_one     = { ... }       # override for _one limits
 *              action_request_perfork = { ... }       # override for _perfork limits
 *              action_request_global  = { ... }       # override for _global limits
 *              
 *              pool_interval     = (uint_t)'5'        # see below. This parameter is global,
 *                                                       newly created backends overwrite it
 *              tick_interval     = (uint_t)'5'        # see below.
 * 	}
 *
 * Limit behavior:
 *   - Limit checked ones per <pool_interval> (default 5 seconds). Between two checks backends can overconsume limit, but this method
 *     will have less performance loss.
 *   - Ticks collected in <tick_interval> period. One tick is one passed request.
 *
 * @endcode
 */

#define EMODULE 15
#define POOL_INTERVAL_DEFAULT 5
#define TICK_INTERVAL_DEFAULT 5

typedef enum parameter_method {
	PARAMETER_REQUEST,
	PARAMETER_ONE,
	PARAMETER_TICKS,
	
	PARAMETER_DEFAULT = PARAMETER_TICKS
} parameter_method;

typedef enum choose_method {
	METHOD_RANDOM,
	METHOD_FIRST,
	METHOD_LAST,
	METHOD_HIGHEST,
	METHOD_LOWEST,
	
	METHOD_DEFAULT = METHOD_FIRST
} choose_method;

typedef enum action_method {
	ACTION_DESTROY,
	ACTION_REQUEST,
	
	ACTION_DEFAULT = ACTION_REQUEST
} action_method;

typedef struct rule {
	uintmax_t        limit;
	choose_method    mode;
	action_method    action;
	request_t       *action_request;
} rule;

typedef struct pool_userdata {
	// usage params
	uintmax_t        created_on;
	uintmax_t        usage_parameter;
	uintmax_t        usage_ticks_last;  // 
	uintmax_t        usage_ticks_curr;  // no locks for this, coz we don't care about precise info
	uintmax_t        usage_ticks_time;  //
	
	// fork support
	list            *perfork_childs;
	list             perfork_childs_own;
	
	parameter_method parameter;
	hash_t          *parameter_request;
	uintmax_t        tick_interval;
	
	rule             rule_one;
	rule             rule_perfork;
	rule             rule_global;
} pool_userdata;

static uintmax_t        pool_interval          = POOL_INTERVAL_DEFAULT;
static uintmax_t        running_pools          = 0;
static pthread_mutex_t  running_pools_mtx      = PTHREAD_MUTEX_INITIALIZER;
static pthread_t        main_thread;
static list             watch_one;
static list             watch_perfork;
static list             watch_global;

static ssize_t pool_backend_request_cticks(backend_t *backend, request_t *request);
static ssize_t pool_backend_request_died(backend_t *backend, request_t *request);

static parameter_method  pool_string_to_parameter(char *string){ // {{{
	if(string != NULL){
		if(strcasecmp(string, "request") == 0)        return PARAMETER_REQUEST;
		if(strcasecmp(string, "ticks") == 0)          return PARAMETER_TICKS;
		if(strcasecmp(string, "one") == 0)            return PARAMETER_ONE;
	}
	return PARAMETER_DEFAULT;
} // }}}
static choose_method     pool_string_to_method(char *string){ // {{{
	if(string != NULL){
		if(strcasecmp(string, "random") == 0)        return METHOD_RANDOM;
		if(strcasecmp(string, "first") == 0)         return METHOD_FIRST;
		if(strcasecmp(string, "last") == 0)          return METHOD_LAST;
		if(strcasecmp(string, "highest") == 0)       return METHOD_HIGHEST;
		if(strcasecmp(string, "lowest") == 0)        return METHOD_LOWEST;
	}
	return METHOD_DEFAULT;
} // }}}
static action_method     pool_string_to_action(char *string){ // {{{
	if(string != NULL){
		if(strcasecmp(string, "destroy") == 0) return ACTION_DESTROY;
		if(strcasecmp(string, "request") == 0) return ACTION_REQUEST;
	}
	return ACTION_DEFAULT;
} // }}}

static void pool_set_handler(backend_t *backend, f_crwd func){ // {{{
	backend->backend_type_crwd.func_create = func;
	backend->backend_type_crwd.func_set    = func;
	backend->backend_type_crwd.func_get    = func;
	backend->backend_type_crwd.func_delete = func;
	backend->backend_type_crwd.func_move   = func;
	backend->backend_type_crwd.func_count  = func;
} // }}}
uintmax_t   pool_parameter_get(backend_t *backend, pool_userdata *userdata){ // {{{
	switch(userdata->parameter){
		case PARAMETER_ONE:   return (backend->backend_type_crwd.func_create == &pool_backend_request_died) ? 0 : 1;
		case PARAMETER_TICKS: return userdata->usage_ticks_last;
		case PARAMETER_REQUEST:;
			uintmax_t buffer    = 0;
			request_t r_query[] = {
				{ HK(buffer), DATA_PTR_UINTT(&buffer) },
				hash_next(userdata->parameter_request)
			};
			backend_pass(backend, r_query);
			
			return buffer;
		default:
			break;
	};
	return 0;
} // }}}
void        pool_backend_action(backend_t *backend, rule *rule){ // {{{
	pool_userdata        *userdata          = (pool_userdata *)backend->userdata;
	
	switch(rule->action){
		case ACTION_DESTROY:;
			backend_t     *curr;
			
			// set backend mode to died
			pool_set_handler(backend, &pool_backend_request_died);
			
			if(userdata->perfork_childs != &userdata->perfork_childs_own){
				// kill childs for forked chains, keep for initial
				while( (curr = list_pop(&backend->childs)) != NULL)
					backend_destroy(curr);
			}
			
			break;
		case ACTION_REQUEST:;
			backend_pass(backend, rule->action_request);
			break;
		default:
			break;
	};
	
	// update parameter
	userdata->usage_parameter = pool_parameter_get(backend, userdata);
} // }}}
void        pool_group_limit(list *backends, rule *rule, uintmax_t need_lock){ // {{{
	uintmax_t              i, lsz;
	uintmax_t              parameter;
	uintmax_t              parameter_group;
	backend_t             *backend;
	pool_userdata         *backend_ud;
	backend_t            **lchilds;
	
	if(rule->limit == __MAX(uintmax_t)) // no limits
		return;
	
	if(need_lock == 1) list_rdlock(backends);
		
		if( (lsz = list_count(backends)) != 0){
			lchilds = alloca( sizeof(backend_t *) * lsz );
			list_flatten(backends, (void **)lchilds, lsz);
			
repeat:;
			parameter_group = 0;
			
			backend_t *backend_first         = NULL;
			backend_t *backend_last          = NULL;
			backend_t *backend_high          = NULL;
			backend_t *backend_low           = NULL;
			
			for(i=0; i<lsz; i++){
				backend    = lchilds[i];
				backend_ud = (pool_userdata *)backend->userdata;
				
				if( (parameter = backend_ud->usage_parameter) == 0)
					continue;
				
				parameter_group += parameter;
				
				#define pool_kill_check(_backend, _field, _moreless) { \
					if(_backend == NULL){ \
						_backend = backend; \
					}else{ \
						if(backend_ud->_field _moreless ((pool_userdata *)_backend->userdata)->_field) \
							_backend = backend; \
					} \
				}
				pool_kill_check(backend_first,      created_on,       <);
				pool_kill_check(backend_last,       created_on,       >);
				pool_kill_check(backend_low,        usage_parameter,  <);
				pool_kill_check(backend_high,       usage_parameter,  >);
			}
			
			if(parameter_group >= rule->limit){
				switch(rule->mode){
					case METHOD_RANDOM:         backend = lchilds[ random() % lsz ]; break;
					case METHOD_FIRST:          backend = backend_first; break;
					case METHOD_LAST:           backend = backend_last; break;
					case METHOD_HIGHEST:        backend = backend_high; break;
					case METHOD_LOWEST:         backend = backend_low; break;
					default:                    backend = NULL; break;
				};
				if(backend != NULL){
					pool_backend_action(backend, rule);
					goto repeat;
				}
			}
		}
		
	if(need_lock == 1) list_unlock(backends);
	
	return;
} // }}}
void *      pool_main_thread(void *null){ // {{{
	void                  *iter;
	backend_t             *backend;
	pool_userdata         *backend_ud;
	
	while(1){
		// update parameter + limit one
		list_rdlock(&watch_one);
			iter = NULL;
			while( (backend = list_iter_next(&watch_one, &iter)) != NULL){
				backend_ud = (pool_userdata *)backend->userdata;
				
				backend_ud->usage_parameter = pool_parameter_get(backend, backend_ud);
				
				if(backend_ud->usage_parameter > backend_ud->rule_one.limit)
					pool_backend_action(backend, &backend_ud->rule_one);
			}
		list_unlock(&watch_one);
		
		// preform perfork limits
		list_rdlock(&watch_perfork);
			iter = NULL;
			while( (backend = list_iter_next(&watch_perfork, &iter)) != NULL){
				backend_ud = (pool_userdata *)backend->userdata;
				
				pool_group_limit(backend_ud->perfork_childs, &backend_ud->rule_perfork, 1);
			}
		list_unlock(&watch_perfork);
		
		// preform global limits
		list_rdlock(&watch_global);
			iter = NULL;
			while( (backend = list_iter_next(&watch_global, &iter)) != NULL){
				backend_ud = (pool_userdata *)backend->userdata;
				
				pool_group_limit(&watch_global, &backend_ud->rule_global, 0);
			}
		list_unlock(&watch_global);
		
		sleep(pool_interval);
	};
	return NULL;
} // }}}

static int pool_init(backend_t *backend){ // {{{
	ssize_t                ret               = 0;
	pool_userdata         *userdata          = backend->userdata = calloc(1, sizeof(pool_userdata));
	if(userdata == NULL)
		return error("calloc failed");
	
	// global
	pthread_mutex_lock(&running_pools_mtx);
		if(running_pools++ == 0){
			list_init(&watch_one);
			list_init(&watch_perfork);
			list_init(&watch_global);
			
			if(pthread_create(&main_thread, NULL, &pool_main_thread, NULL) != 0)
				ret = error("pthread_create failed");
		}
	pthread_mutex_unlock(&running_pools_mtx);
	
	return ret;
} // }}}
static int pool_destroy(backend_t *backend){ // {{{
	void                  *res;
	ssize_t                ret               = 0;
	pool_userdata         *userdata          = (pool_userdata *)backend->userdata;
	
	if(userdata->perfork_childs == &userdata->perfork_childs_own){
		list_destroy(&userdata->perfork_childs_own);
	}else{
		list_delete(userdata->perfork_childs, backend);
	}
	hash_free(userdata->parameter_request);
	hash_free(userdata->rule_one.action_request);
	hash_free(userdata->rule_perfork.action_request);
	hash_free(userdata->rule_global.action_request);
	list_delete(&watch_one,     backend);
	list_delete(&watch_perfork, backend);
	list_delete(&watch_global,  backend);
	free(userdata);
	
	// global
	pthread_mutex_lock(&running_pools_mtx);
		if(--running_pools == 0){
			if(pthread_cancel(main_thread) != 0){
				ret = error("pthread_cancel failed");
				goto exit;
			}
			if(pthread_join(main_thread, &res) != 0){
				ret = error("pthread_join failed");
				goto exit;
			}
			// TODO memleak here
			
			list_destroy(&watch_one);
			list_destroy(&watch_perfork);
			list_destroy(&watch_global);
		}
exit:
	pthread_mutex_unlock(&running_pools_mtx);
	
	return ret;
} // }}}
static int pool_configure_any(backend_t *backend, backend_t *parent, config_t *config){ // {{{
	ssize_t                ret;
	uintmax_t              cfg_limit_one     = __MAX(uintmax_t);
	uintmax_t              cfg_limit_fork    = __MAX(uintmax_t);
	uintmax_t              cfg_limit_global  = __MAX(uintmax_t);
	uintmax_t              cfg_tick          = TICK_INTERVAL_DEFAULT;
	uintmax_t              cfg_pool          = POOL_INTERVAL_DEFAULT;
	char                  *cfg_parameter     = NULL;
	char                  *cfg_act           = NULL;
	char                  *cfg_act_one       = NULL;
	char                  *cfg_act_perfork   = NULL;
	char                  *cfg_act_global    = NULL;
	char                  *cfg_mode_perfork  = NULL;
	char                  *cfg_mode_global   = NULL;
	hash_t                 cfg_act_req_def[] = { hash_end };
	hash_t                *cfg_act_req       = cfg_act_req_def;
	hash_t                *cfg_act_req_one;
	hash_t                *cfg_act_req_frk;
	hash_t                *cfg_act_req_glb;
	hash_t                *cfg_param_req     = cfg_act_req_def;
	pool_userdata         *userdata          = (pool_userdata *)backend->userdata;
	
	hash_data_copy(ret, TYPE_STRINGT, cfg_parameter,    config, HK(parameter));
	hash_data_copy(ret, TYPE_STRINGT, cfg_mode_perfork, config, HK(mode_perfork));
	hash_data_copy(ret, TYPE_STRINGT, cfg_mode_global,  config, HK(mode_global));
	hash_data_copy(ret, TYPE_UINTT,   cfg_tick,         config, HK(tick_interval));
	
	// actions
	hash_data_copy(ret, TYPE_STRINGT, cfg_act,          config, HK(action));
	cfg_act_one = cfg_act;
	cfg_act_perfork = cfg_act;
	cfg_act_global = cfg_act;
	hash_data_copy(ret, TYPE_STRINGT, cfg_act_one,      config, HK(action_one));
	hash_data_copy(ret, TYPE_STRINGT, cfg_act_perfork,  config, HK(action_perfork));
	hash_data_copy(ret, TYPE_STRINGT, cfg_act_global,   config, HK(action_global));
	
	// requests
	hash_data_copy(ret, TYPE_HASHT,   cfg_param_req,    config, HK(parameter_request));
	
	hash_data_copy(ret, TYPE_HASHT,   cfg_act_req,      config, HK(action_request));
	cfg_act_req_one = cfg_act_req;
	cfg_act_req_frk = cfg_act_req;
	cfg_act_req_glb = cfg_act_req;
	hash_data_copy(ret, TYPE_HASHT,   cfg_act_req_one,  config, HK(action_request_one));
	hash_data_copy(ret, TYPE_HASHT,   cfg_act_req_frk,  config, HK(action_request_perfork));
	hash_data_copy(ret, TYPE_HASHT,   cfg_act_req_glb,  config, HK(action_request_global));
	
	hash_data_copy(ret, TYPE_UINTT,   cfg_pool,         config, HK(pool_interval));
	if(ret != 0)
		pool_interval = cfg_pool;
	
	userdata->parameter                   = pool_string_to_parameter(cfg_parameter);
	userdata->parameter_request           = hash_copy(cfg_param_req);
	userdata->rule_perfork.mode           = pool_string_to_method(cfg_mode_perfork);
	userdata->rule_global.mode            = pool_string_to_method(cfg_mode_global);
	userdata->rule_one.action             = pool_string_to_action(cfg_act_perfork);
	userdata->rule_perfork.action         = pool_string_to_action(cfg_act_perfork);
	userdata->rule_global.action          = pool_string_to_action(cfg_act_global);
	userdata->rule_one.action_request     = hash_copy(cfg_act_req_one);
	userdata->rule_perfork.action_request = hash_copy(cfg_act_req_frk);
	userdata->rule_global.action_request  = hash_copy(cfg_act_req_glb);
	userdata->tick_interval               = cfg_tick;
	userdata->created_on                  = time(NULL);
	
	hash_data_copy(ret, TYPE_UINTT,   cfg_limit_one,    config, HK(max_perinstance));
	userdata->rule_one.limit              = cfg_limit_one;
	list_add(&watch_one, backend);
	
	hash_data_copy(ret, TYPE_UINTT,   cfg_limit_fork,   config, HK(max_perfork));
	userdata->rule_perfork.limit          = cfg_limit_fork;
	if(ret == 0)
		list_add(&watch_perfork, (parent == NULL) ? backend : parent);
	
	hash_data_copy(ret, TYPE_UINTT,   cfg_limit_global, config, HK(max_global));
	userdata->rule_global.limit           = cfg_limit_global;
	if(ret == 0)
		list_add(&watch_global,  backend);
	
	if(userdata->parameter == PARAMETER_TICKS){
		pool_set_handler(backend, &pool_backend_request_cticks);
	}else{
		pool_set_handler(backend, NULL);
	}
	
	return 0;
} // }}}
static int pool_configure(backend_t *backend, config_t *config){ // {{{
	pool_userdata        *userdata          = (pool_userdata *)backend->userdata;
	
	// no fork, so use own data
	userdata->perfork_childs       = &userdata->perfork_childs_own;
	list_init(&userdata->perfork_childs_own);
	
	return pool_configure_any(backend, NULL, config);
} // }}}
static int pool_fork(backend_t *backend, backend_t *parent, config_t *config){ // {{{
	pool_userdata        *userdata_parent   = (pool_userdata *)parent->userdata;
	pool_userdata        *userdata          = (pool_userdata *)backend->userdata;
	
	// have fork, so use parent data
	userdata->perfork_childs       = &userdata_parent->perfork_childs_own;
	list_add(userdata->perfork_childs, backend);
	
	return pool_configure_any(backend, parent, backend->config);
} // }}}

static ssize_t pool_backend_request_cticks(backend_t *backend, request_t *request){ // {{{
	ssize_t                ret;
	uintmax_t              time_curr;
	pool_userdata         *userdata = (pool_userdata *)backend->userdata;
	
	// update usage ticks
	
	time_curr = time(NULL);
	if(userdata->usage_ticks_time + userdata->tick_interval < time_curr){
		userdata->usage_ticks_time = time_curr;
		userdata->usage_ticks_last = userdata->usage_ticks_curr;
		userdata->usage_ticks_curr = 0;
	}
	userdata->usage_ticks_curr++;
	
	return ( (ret = backend_pass(backend, request)) < 0) ? ret : -EEXIST;
} // }}}
static ssize_t pool_backend_request_died(backend_t *backend, request_t *request){ // {{{
	return -EBADF;
} // }}}

backend_t pool_proto = {
	.class          = "backend/pool",
	.supported_api  = API_CRWD,
	.func_init      = &pool_init,
	.func_configure = &pool_configure,
	.func_fork      = &pool_fork,
	.func_destroy   = &pool_destroy
};

