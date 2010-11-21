#include <libfrozen.h>

/*
m4_define(`BYTES', m4_eval(BITS() `/ 8'))
m4_ifelse(BYTES(), `1', `m4_define(`TYPE', `unsigned char')')
m4_ifelse(BYTES(), `2', `m4_define(`TYPE', `unsigned short')')
m4_ifelse(BYTES(), `4', `m4_define(`TYPE', `unsigned int')')
m4_ifelse(BYTES(), `8', `m4_define(`TYPE', `unsigned long long')')

m4_ifelse(NAME(), `size_t', `m4_define(`TYPE', `size_t')') 
m4_ifelse(NAME(), `size_t', `m4_define(`BYTES', `sizeof(size_t)')')

m4_ifelse(NAME(), `off_t',  `m4_define(`TYPE', `off_t')') 
m4_ifelse(NAME(), `off_t',  `m4_define(`BYTES', `sizeof(off_t)')') 
m4_changequote([,])
*/
#ifndef __MAX
	#define __HALF_MAX_SIGNED(type) ((type)1 << (sizeof(type)*8-2))
	#define __MAX_SIGNED(type) (__HALF_MAX_SIGNED(type) - 1 + __HALF_MAX_SIGNED(type))
	#define __MIN_SIGNED(type) (-1 - __MAX_SIGNED(type))
	#define __MIN(type) ((type)-1 < 1?__MIN_SIGNED(type):(type)0)
	#define __MAX(type) ((type)~__MIN(type))
#endif

[int data_]NAME()_cmp(data_t *data1, data_ctx_t *ctx1, data_t *data2, data_ctx_t *ctx2){ // {{{
	int           cret;
	TYPE          data1_val, data2_val;
	
	if(data1->data_size < BYTES() || data2->data_size < BYTES())
		return -EINVAL;
	
	data1_val = *(TYPE *)(data1->data_ptr);
	data2_val = *(TYPE *)(data2->data_ptr); 
	     if(data1_val == data2_val){ cret =  0; }
	else if(data1_val <  data2_val){ cret = -1; }
	else                           { cret =  1; }
	
	return cret;
} // }}}

[int data_]NAME()_arith(char operator, data_t *operand1, data_ctx_t *ctx1, data_t *operand2, data_ctx_t *ctx2){ // {{{
	int           ret = 0;
	TYPE          operand1_val, operand2_val, result;
	
	if(operand1->data_size < BYTES() || operand2->data_size < BYTES())
		return -EINVAL;
	
	operand1_val = *(TYPE *)(operand1->data_ptr);
	operand2_val = *(TYPE *)(operand2->data_ptr); 
	switch(operator){
		case '+':
			if(__MAX(TYPE) - operand1_val < operand2_val)
				ret = -EOVERFLOW;
			
			result = operand1_val + operand2_val;
			break;
		case '-':
			if(__MIN(TYPE) + operand2_val > operand1_val)
				ret = -EOVERFLOW;
			
			result = operand1_val - operand2_val;
			break;
		case '*':
			if(operand2_val == 0){
				result = 0;
			}else{
				if(operand1_val > __MAX(TYPE) / operand2_val)
					ret = -EOVERFLOW;
				
				result = operand1_val * operand2_val;
			}
			break;
		case '/':
			if(operand2_val == 0)
				return -EINVAL;
			
			result = operand1_val / operand2_val;
			break;
		default:
			return -1;
	}
	*(TYPE *)(operand1->data_ptr) = result;
	return ret;
} // }}}

/*
REGISTER_TYPE([TYPE_]DEF())
REGISTER_MACRO([DATA_]DEF()[(value)],     `[TYPE_]DEF()[, (]TYPE()[ []){ value }, sizeof(]TYPE()[)]')
REGISTER_MACRO([DATA_PTR_]DEF()[(value)], `[TYPE_]DEF()[, value, sizeof(]TYPE()[)]')
REGISTER_PROTO([
	`{
		.type                   = TYPE_]DEF()[,
		.type_str               = "]NAME()[",
		.size_type              = SIZE_FIXED,
		.func_cmp               = &data_]NAME()[_cmp,
		.func_arithmetic        = &data_]NAME()[_arith,
		.fixed_size             = ]BYTES()[
	}'
])
*/
/* vim: set filetype=c: */