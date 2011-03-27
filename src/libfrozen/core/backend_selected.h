/* Autogenerated file. Don't edit */
#ifndef BACKEND_SELECTED_H
#define BACKEND_SELECTED_H
extern backend_t allocator_proto;
extern backend_t cache_proto;
extern backend_t cache_append_proto;
extern backend_t file_proto;
extern backend_t incapsulate_proto;
extern backend_t insert_sort_proto;
extern backend_t ipc_proto;
extern backend_t list_proto;
extern backend_t mphf_proto;
extern backend_t null_proto;
extern backend_t rewrite_proto;
extern backend_t structs_proto;
#ifdef BACKEND_C
backend_t * backend_protos[] = {
   &allocator_proto,
   &cache_proto,
   &cache_append_proto,
   &file_proto,
   &incapsulate_proto,
   &insert_sort_proto,
   &ipc_proto,
   &list_proto,
   &mphf_proto,
   &null_proto,
   &rewrite_proto,
   &structs_proto,
};
size_t backend_protos_size = (sizeof(backend_protos)/sizeof(backend_protos[0]));
#endif
#endif
