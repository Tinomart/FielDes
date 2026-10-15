// This definition lives in its own header so we can include it
// in both eval_array.hpp and oracle.hpp, since having either
// include the other causes horrible include loops.
// (not a power of two, on purpose: a row of the values is 1088 bytes, 17 cache lines, so that successive rows fall in successive different sets of the
// cache.  A row of 1024 bytes put all of a tape's rows in the same few sets, and a pass of 8 points -- one cache line of each row -- evicted itself)
#define LIBFIVE_EVAL_ARRAY_SIZE 272
