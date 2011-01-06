#include <test.h>

START_TEST (test_backend_mphf){
	hash_t config[] = {
		{ NULL, DATA_HASHT(
			{ "name",    DATA_STRING("backend_mphf_idx")                        },
			{ "chains",  DATA_HASHT(
				{ NULL, DATA_HASHT(
					{ "name",      DATA_STRING("file")                  },
					{ "filename",  DATA_STRING("data_backend_mphi.dat") },
					hash_end
				)},
				hash_end
			)},
			hash_end
		)},
		{ NULL, DATA_HASHT(
			{ "name",   DATA_STRING("backend_mphf")                             },
			{ "chains", DATA_HASHT(
				{ NULL, DATA_HASHT(
					{ "name",      DATA_STRING("file")                  },
					{ "filename",  DATA_STRING("data_backend_mphf.dat") },
					hash_end
				)},
				{ NULL, DATA_HASHT(
					{ "name",      DATA_STRING("mphf")                  },
					{ "mphf_type", DATA_STRING("chm_imp")               },
					{ "backend",   DATA_STRING("backend_mphf_idx")      },
					hash_end
				)},
				hash_end
			)},
			hash_end
		)},
		hash_end
	};
	
	backend_bulk_new(config);
	
	data_t     n_dat = DATA_STRING("backend_mphf"), n_idx = DATA_STRING("backend_mphf_idx");
	backend_t *b_dat = backend_find(&n_dat);
	backend_t *b_idx = backend_find(&n_idx);
	
	char  *data_array[] = {
		"http://google.ru/",
		"http://yandex.ru/",
		"http://bing.com/",
		"http://rambler.ru/",
		"http://aport.ru/",
		"http://hell.com/"
	};
	
	// write array to file
	int      i;
	ssize_t  ret;
	
	for(i=0; i < sizeof(data_array) / sizeof(char *); i++){
		request_t r_write[] = {
			{ "action",  DATA_INT32 (ACTION_CRWD_WRITE)                           },
			{ "key",     DATA_PTR_STRING (data_array[i], strlen(data_array[i])+1) },
			{ "buffer",  DATA_PTR_STRING (data_array[i], strlen(data_array[i])+1) },
			hash_end
		};
		ret = backend_query(b_dat, r_write);
			fail_unless(ret > 0, "chain 'backend_mphf': write array failed");
	}
	
	// check
	char data_read[1024];
	for(i=0; i < sizeof(data_array) / sizeof(char *); i++){
		request_t r_read[] = {
			{ "action", DATA_INT32(ACTION_CRWD_READ)                              },
			{ "key",    DATA_PTR_STRING (data_array[i], strlen(data_array[i])+1)  },
			{ "buffer", DATA_PTR_STRING(&data_read, 1024)                         },
			hash_end
		};
		ret = backend_query(b_dat, r_read);
			fail_unless(ret > 0,                               "chain 'backend_mphf': read array failed");
			fail_unless(strcmp(data_read, data_array[i]) == 0, "chain 'backend_mphf': read array data failed");
	}
	
	backend_destroy(b_dat);
	backend_destroy(b_idx);
}
END_TEST
REGISTER_TEST(core, test_backend_mphf)

