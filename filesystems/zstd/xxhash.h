/*
 * XXH64 streaming hash state and function interface. Its checksums are for
 * format consistency, not cryptographic authentication of EFI boot media.
 */

#ifndef XXHASH_H
#define XXHASH_H

uint64_t xxh64(const void *input, size_t length, uint64_t seed);

/*
 * The four lanes accumulate 32-byte stripes. total_len and the buffered tail
 * let digest finish the hash without requiring the caller to preserve earlier
 * portions of the decoded frame.
 */
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
