/*
 * The caller enters the fast loop with at least six input bytes and room for
 * a 258-byte match. start is the output capacity at entry to the enclosing
 * inflate call and is used to find the boundary with saved history.
 */
 




 




void inflate_fast (z_streamp strm, unsigned start);
