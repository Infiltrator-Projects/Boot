/*
 * Fast DEFLATE decoder declaration. Its use requires the caller to have
 * validated Huffman tables and established the minimum input/output space
 * expected by the unrolled decoding loop.
 */
 




 




void inflate_fast (z_streamp strm, unsigned start);
