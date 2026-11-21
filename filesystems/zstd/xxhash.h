 







































 

































#ifndef XXHASH_H
#define XXHASH_H

 










uint64_t xxh64(const void *input, size_t length, uint64_t seed);

 



 





 


struct xxh64_state {
	uint64_t total_len;
	uint64_t v1;
	uint64_t v2;
	uint64_t v3;
	uint64_t v4;
	uint64_t mem64[4];
	uint32_t memsize;
};

 





void xxh64_reset(struct xxh64_state *state, uint64_t seed);

 









int xxh64_update(struct xxh64_state *state, const void *input, size_t length);

 










uint64_t xxh64_digest(const struct xxh64_state *state);

 



#endif  
