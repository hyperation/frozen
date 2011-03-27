#include <libfrozen.h>
#include <alloca.h>

#define EMODULE         1
#define DEF_BUFFER_SIZE 1024

typedef enum file_stat_status {
	STAT_NEED_UPDATE,
	STAT_UPDATED,
	STAT_ERROR
} file_stat_status;

typedef struct file_userdata {
	char *           path;
	int              handle;
	size_t           buffer_size;
	struct stat      file_stat;
	file_stat_status file_stat_status;
	
	pthread_mutex_t  create_lock;
	
	data_t           file_io;
} file_userdata;


// IO's
static ssize_t file_io_write(data_t *data, data_ctx_t *context, off_t offset, void *buffer, size_t size){ // {{{
	ssize_t      ret;
	DT_UINT32T     handle;
	DT_OFFT      key      = 0;
	DT_SIZET     size_min = size;
	
	(void)data;
	
	hash_data_copy(ret, TYPE_UINT32T, handle,    context, HK(handle)); if(ret != 0) return error("handle not supplied");
	hash_data_copy(ret, TYPE_OFFT,  key,       context, HK(offset));
	hash_data_copy(ret, TYPE_SIZET, size_min,  context, HK(size));
	
	// apply size limits
	size_min = MIN(size, size_min);
	
redo:   ret = pwrite(
		handle,
		buffer,
		size_min,
		key + offset
	);
	if(ret == -1){
		if(errno == EINTR) goto redo;
	}
	return ret;
} // }}}
static ssize_t file_io_read (data_t *data, data_ctx_t *context, off_t offset, void **buffer, size_t *buffer_size){ // {{{
	ssize_t    ret;
	DT_UINT32T   handle;
	DT_OFFT    key  = 0;
	DT_SIZET   size = *buffer_size;
	
	(void)data;
	
	hash_data_copy(ret, TYPE_UINT32T, handle,    context, HK(handle)); if(ret != 0) return error("handle not supplied");
	hash_data_copy(ret, TYPE_OFFT,  key,       context, HK(offset));
	hash_data_copy(ret, TYPE_SIZET, size,      context, HK(size));
	
	if(offset >= size)
		return -1; // EOF
	
	size = (size > *buffer_size) ? *buffer_size : size;
		
redo:   ret = pread(
		handle,
		*buffer,
		size,
		key + offset
	);
	if(ret == -1){
		if(errno == EINTR) goto redo;
		return -errno;
	}
	if(ret == 0 && size != 0)
		return -1; // EOF
	
	*buffer_size = ret;
	return ret;
} // }}}

data_t file_io = DATA_IOT(
	file_io_read,
	file_io_write
);

// internal functions
static int               file_new_offset(backend_t *backend, off_t *new_key, unsigned int size){ // {{{
	int               fd;
	int               ret;
	off_t             key;
	pthread_mutex_t  *mutex;
	file_userdata   *data        = ((file_userdata *)backend->userdata);
	
	fd    =  data->handle;
	mutex = &data->create_lock;
	
	pthread_mutex_lock(mutex);
		
		key = lseek(fd, 0, SEEK_END);
		ret = ftruncate(fd, key + size);
		if(ret != -1){
			data->file_stat_status = STAT_NEED_UPDATE;
			*new_key               = key;
			ret                    = 0;
		}else{
			ret                    = -errno;
		}
		
	pthread_mutex_unlock(mutex);
	
	return ret;
} // }}}
static file_stat_status  file_update_count(file_userdata *data){ // {{{
	int              ret;
	
	switch(data->file_stat_status){
		case STAT_NEED_UPDATE:
		case STAT_ERROR:
			ret = fstat(
		 		data->handle,
				&data->file_stat
			);
			data->file_stat_status = (ret == 0) ? STAT_UPDATED : STAT_ERROR;
			break;
		case STAT_UPDATED:
			break;
	};
	return data->file_stat_status;
} // }}}

// Init and destroy
static int file_init(backend_t *backend){ // {{{
	file_userdata *userdata = calloc(1, sizeof(file_userdata));
	if(userdata == NULL)
		return error("calloc returns null");
	
	backend->userdata = userdata;
	
	return 0;
} // }}}
static int file_destroy(backend_t *backend){ // {{{
	file_userdata *data = (file_userdata *)backend->userdata;
	
	if(data->path){
		// was inited
		// TODO rewrite condition
		
		free(data->path);
		close(data->handle);
		pthread_mutex_destroy(&(data->create_lock));
	}
	
	free(backend->userdata);
	
	backend->userdata = NULL;
	
	return 0;
} // }}}
static int file_configure(backend_t *backend, hash_t *config){ // {{{
	ssize_t        ret;
	int            handle;
	size_t         s;
	char          *filepath;
	char          *filename;
	char          *homedir;
	size_t         buffer_size = DEF_BUFFER_SIZE;
	file_userdata *userdata    = (file_userdata *)backend->userdata;
	
	hash_data_copy(ret, TYPE_SIZET,  buffer_size, config, HK(buffer_size));
	hash_data_copy(ret, TYPE_STRINGT, filename,    config, HK(filename));
	if(ret != 0)
		return error("filename not defined");
	
	hash_data_copy(ret, TYPE_STRINGT, homedir,     global_settings, HK(homedir));
	if(ret != 0)
		homedir = ".";
	
	s = strlen(homedir) + 1 + strlen(filename) + 1;
	
	if((filepath = malloc(s)) == NULL)
		return error("filename too long");
	
	/* snprintf can truncate strings, so malicious user can use it to access another file.
	 * we check string len such way to ensure none of strings was truncated */
	if(snprintf(filepath, s, "%s/%s", homedir, filename) >= s){
		ret = error("filename too long");
		goto cleanup;
	}
	
	// TODO add realpath and checks
	
	handle = open(
		filepath,
		O_CREAT | O_RDWR | O_LARGEFILE,
		S_IRUSR | S_IWUSR
	);
	
	if(handle == -1){
		ret = error("file open() error");
		goto cleanup;
	}
	
	userdata->path             = filepath;
	userdata->handle           = handle;
	userdata->buffer_size      = buffer_size;
	userdata->file_stat_status = STAT_NEED_UPDATE;
	
	pthread_mutex_init(&(userdata->create_lock), NULL);
	return 0;

cleanup:
	free(filepath);
	return ret;
} // }}}

// Requests
static ssize_t file_write(backend_t *backend, request_t *request){ // {{{
	data_t           *buffer;
	data_ctx_t       *buffer_ctx;
	file_userdata    *data        = ((file_userdata *)backend->userdata);
	
	data->file_stat_status = STAT_NEED_UPDATE;
	
	hash_data_find(request, HK(buffer), &buffer, &buffer_ctx);
	data_ctx_t  file_ctx[] = {
		{ HK(handle), DATA_UINT32T(data->handle) },
		hash_next(request)
	};
	
	return data_transfer(&file_io, file_ctx, buffer, buffer_ctx);
} // }}}
static ssize_t file_read(backend_t *backend, request_t *request){ // {{{
	data_t           *buffer;
	data_ctx_t       *buffer_ctx;
	file_userdata    *data        = ((file_userdata *)backend->userdata);
	
	data->file_stat_status = STAT_NEED_UPDATE;
	
	hash_data_find(request, HK(buffer), &buffer, &buffer_ctx);
	data_ctx_t  file_ctx[] = {
		{ HK(handle), DATA_UINT32T(data->handle) },
		hash_next(request)
	};
	
	return data_transfer(buffer, buffer_ctx, &file_io, file_ctx);
} // }}}
static ssize_t file_create(backend_t *backend, request_t *request){ // {{{
	DT_SIZET          size;
	ssize_t           ret;
	off_t             offset;
	data_t            offset_data = DATA_PTR_OFFT(&offset);
	data_t           *key_out;
	data_ctx_t       *key_out_ctx;
	
	hash_data_copy(ret, TYPE_SIZET, size, request, HK(size)); if(ret != 0) return warning("size not supplied");
	
	if( file_new_offset(backend, &offset, size) != 0)
		return error("file expand error");
	
	/* optional offset fill */
	hash_data_find(request, HK(offset_out), &key_out, &key_out_ctx);
	data_transfer(key_out, key_out_ctx, &offset_data, NULL);
	
	/* optional write from buffer */
	request_t r_write[] = {
		{ HK(offset), offset_data },
		hash_next(request)
	};
	if( (ret = file_write(backend, r_write)) != -EINVAL)
		return ret;
	
	return 0;
} // }}}
static ssize_t file_delete(backend_t *backend, request_t *request){ // {{{
	ssize_t           ret;
	int               fd;
	DT_OFFT           key;
	DT_SIZET          size;
	DT_UINT32T          forced = 0;
	file_userdata    *data        = ((file_userdata *)backend->userdata);
	
	hash_data_copy(ret, TYPE_UINT32T, forced, request, HK(forced));
	hash_data_copy(ret, TYPE_OFFT,  key,    request, HK(offset));  if(ret != 0) return warning("offset not supplied");
	hash_data_copy(ret, TYPE_SIZET, size,   request, HK(size));    if(ret != 0) return warning("size not supplied");
	
	fd  = data->handle;
	ret = 0;
	pthread_mutex_lock(&data->create_lock);
		
		if(file_update_count(data) == STAT_ERROR){
			ret = error("file_update_count failed");
			goto exit;
		}
		
		if(
			key + size != data->file_stat.st_size
			&& forced == 0
		){ // truncating only last elements
			ret = warning("cant delete not last elements");
			goto exit;
		}
		
		if(ftruncate(fd, key) == -1){
			ret = -errno;
			goto exit;
		}
		
		data->file_stat_status = STAT_NEED_UPDATE;
		
exit:		
	pthread_mutex_unlock(&data->create_lock);
	return 0;
} // }}}
static ssize_t file_move(backend_t *backend, request_t *request){ // {{{
	size_t           size, max_size;
	ssize_t          ssize, ret;
	char *           buff;
	int              direction;
	file_userdata   *data        = ((file_userdata *)backend->userdata);
	DT_OFFT          from, to;
	DT_SIZET         move_size = -1;
	
	hash_data_copy(ret, TYPE_OFFT,   from,      request, HK(offset_from)); if(ret != 0) return warning("offset_from not supplied");
	hash_data_copy(ret, TYPE_OFFT,   to,        request, HK(offset_to));   if(ret != 0) return warning("offset_to not supplied");
	hash_data_copy(ret, TYPE_SIZET,  move_size, request, HK(size));
	
	if(move_size == -1){
		if(file_update_count(data) == STAT_ERROR)
			return error("file_update_count failed");
		
		move_size = (data->file_stat.st_size - from);
	}
	
	if(from == to || move_size == 0)
		return 0;
	
	if(from < to){
		max_size   = ((to - from) > data->buffer_size) ? data->buffer_size : (size_t)(to - from);
		size       = (max_size < move_size)            ? max_size          : move_size;
		
		from      += move_size - size;
		to        += move_size - size;
		direction  = -1;
	}else{
		max_size   = ((from - to) > data->buffer_size) ? data->buffer_size : (size_t)(from - to);
		size       = (max_size < move_size)            ? max_size          : move_size;
			
		direction  = 1;
	}
	
	if(size == 0)
		return warning("size is null");
	
	if( (buff = malloc(max_size)) == NULL)
		return error("malloc error");
	
	ret = 0;
	while(move_size > 0){
		if( (ssize = pread(data->handle, buff, size, from)) == -1){
			if(errno == EINTR) continue;
			ret = -errno;
			break;
		}
		if(ssize == 0){
			ret = error("io error");
			break;
		}
		
		if( (ssize = pwrite(data->handle, buff, ssize, to) == -1)){
			if(errno == EINTR) continue;
			ret = -errno;
			break;
		}
		if(ssize == 0){
			// TODO what the hell, pwrite returns 0 in any case
			//ret = -EINVAL;
			//break;
		}
		
		if(direction ==  1){ from += size; }else{ from -= size; }
		if(direction ==  1){ to   += size; }else{ to   -= size; }
		move_size  -= size;
		size        = (max_size < move_size) ? max_size : move_size;
	}
	free(buff);
	
	data->file_stat_status = STAT_NEED_UPDATE;
	
	return ret;
} // }}}
static ssize_t file_count(backend_t *backend, request_t *request){ // {{{
	data_t          *buffer;
	data_ctx_t      *buffer_ctx;
	file_userdata   *data = ((file_userdata *)backend->userdata);
	
	hash_data_find(request, HK(buffer), &buffer, &buffer_ctx);
	if(buffer == NULL)
		return warning("no buffer supplied");
	
	if(file_update_count(data) == STAT_ERROR)
		return error("file_update_count failed");
	
	data_t count = DATA_PTR_OFFT(&(data->file_stat.st_size));
	
	return data_transfer(
		buffer, buffer_ctx,
		&count,  NULL
	);
} // }}}

backend_t file_proto = {
	.class          = "file",
	.supported_api  = API_CRWD,
	.func_init      = &file_init,
	.func_configure = &file_configure,
	.func_destroy   = &file_destroy,
	{{
		.func_create = &file_create,
		.func_set    = &file_write,
		.func_get    = &file_read,
		.func_delete = &file_delete,
		.func_move   = &file_move,
		.func_count  = &file_count
	}}
};

