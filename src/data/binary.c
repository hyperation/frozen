#include <libfrozen.h>

typedef struct data_binary_t {
	unsigned int size;
	// data here
} data_binary_t;

int     data_binary_cmp(void *data1, void *data2){
	int           cret;
	unsigned int  item1_len, item2_len;	
	
	item1_len = ((data_binary_t *)data1)->size;
	item2_len = ((data_binary_t *)data2)->size;
	     if(item1_len > item2_len){ cret =  1; }
	else if(item1_len < item2_len){ cret = -1; }
	else {
		cret = memcmp((data_binary_t *)data1 + 1, (data_binary_t *)data2 + 1, item1_len);
		if(cret > 0) cret =  1;
		if(cret < 0) cret = -1;
	}
	return cret;
}

size_t  data_binary_raw_len(void *data, size_t buffer_size){
	size_t data_size;
	
	data_size = (size_t)(((data_binary_t *)data)->size);
	if(data_size < sizeof(data_binary_t) || data_size > buffer_size)
		return 0;
	
	return data_size;
}

size_t  data_binary_len(void *data){
	size_t data_size;
	
	data_size = (size_t)(((data_binary_t *)data)->size);
	
	return (data_size - sizeof(data_binary_t));
}

void *  data_binary_ptr(void *data){
	return (void *)((data_binary_t *)data + 1);
}

REGISTER_DATA(TYPE_BINARY,SIZE_VARIABLE, .func_cmp = &data_binary_cmp, .func_len = &data_binary_raw_len)

