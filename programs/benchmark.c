/*
 * benchmark.c - a compression testing and benchmark program
 *
 * Copyright 2016 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "prog_util.h"

static const tchar *const optstring = T("123456789hL:s:V");

static void
show_usage(FILE *fp)
{
	fprintf(fp,
"Usage: %"TS" [-123456789hV] [-L LVL] [-s SIZE] [FILE]...\n"
"Benchmark XPACK compression and decompression on the specified FILEs.\n"
"\n"
"Options:\n"
"  -1        fastest (worst) compression\n"
"  -9        slowest (best) compression\n"
"  -h        print this help\n"
"  -L LVL    compression level [1-9] (default 6)\n"
"  -s SIZE   chunk size (default 524288)\n"
"  -V        show version and legal information\n",
	program_invocation_name);
}

static void
show_version(void)
{
	printf(
"XPACK compression benchmark program, experimental version\n"
"Copyright 2016 Eric Biggers\n"
"\n"
"This program is free software which may be modified and/or redistributed\n"
"under the terms of the MIT license.  There is NO WARRANTY, to the extent\n"
"permitted by law.  See the COPYING file for details.\n"
	);
}

static int
do_benchmark(struct file_stream *in, void *original_buf, void *compressed_buf,
	     void *decompressed_buf, u32 chunk_size,
	     struct xpack_compressor *compressor,
	     struct xpack_decompressor *decompressor)
{
	u64 total_uncompressed_size = 0;
	u64 total_compressed_size = 0;
	u64 total_compress_time = 0;
	u64 total_decompress_time = 0;
	ssize_t ret;

	while ((ret = xread(in, original_buf, chunk_size)) > 0) {
		u32 original_size = ret;
		u32 compressed_size;
		u64 start_time;
		enum decompress_result result;

		total_uncompressed_size += original_size;

		/* Compress the chunk of data. */
		start_time = current_time();
		compressed_size = xpack_compress(compressor,
						 original_buf,
						 original_size,
						 compressed_buf,
						 original_size - 1);
		total_compress_time += current_time() - start_time;

		if (compressed_size) {
			/* Successfully compressed the chunk of data. */

			/* Decompress the data we just compressed and compare
			 * the result with the original. */
			start_time = current_time();
			result = xpack_decompress(decompressor,
						  compressed_buf,
						  compressed_size,
						  decompressed_buf,
						  original_size,
						  NULL);
			total_decompress_time += current_time() - start_time;

			if (result != DECOMPRESS_SUCCESS) {
				msg("%"TS": failed to decompress data",
				    in->name);
				return -1;
			}

			if (memcmp(original_buf, decompressed_buf,
				   original_size) != 0)
			{
				msg("%"TS": data did not decompress to "
				    "original", in->name);
				return -1;
			}

			total_compressed_size += compressed_size;
		} else {
			/* Compression did not make the chunk smaller. */
			total_compressed_size += original_size;
		}
	}

	if (ret < 0)
		return ret;

	if (total_uncompressed_size == 0) {
		printf("\tFile was empty.\n");
		return 0;
	}

	if (total_compress_time == 0)
		total_compress_time = 1;
	if (total_decompress_time == 0)
		total_decompress_time = 1;

	printf("\tCompressed %"PRIu64 " => %"PRIu64" bytes (%u.%03u%%)\n",
	       total_uncompressed_size, total_compressed_size,
	       (unsigned int)(total_compressed_size * 100 /
				total_uncompressed_size),
	       (unsigned int)(total_compressed_size * 100000 /
				total_uncompressed_size % 1000));
	printf("\tCompression time: %"PRIu64" ms (%"PRIu64" MB/s)\n",
	       total_compress_time / 1000000,
	       1000 * total_uncompressed_size / total_compress_time);
	printf("\tDecompression time: %"PRIu64" ms (%"PRIu64" MB/s)\n",
	       total_decompress_time / 1000000,
	       1000 * total_uncompressed_size / total_decompress_time);

	return 0;
}

int
tmain(int argc, tchar *argv[])
{
	u32 chunk_size = 524288;
	int compression_level = 6;
	void *original_buf = NULL;
	void *compressed_buf = NULL;
	void *decompressed_buf = NULL;
	struct xpack_compressor *compressor = NULL;
	struct xpack_decompressor *decompressor = NULL;
	tchar *default_file_list[] = { NULL };
	int opt_char;
	int i;
	int ret;

	program_invocation_name = get_filename(argv[0]);

	while ((opt_char = tgetopt(argc, argv, optstring)) != -1) {
		switch (opt_char) {
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			compression_level = opt_char - '0';
			break;
		case 'h':
			show_usage(stdout);
			return 0;
		case 'L':
			compression_level = parse_compression_level(toptarg);
			if (compression_level <= 0)
				return 1;
			break;
		case 's':
			chunk_size = parse_chunk_size(toptarg);
			if (chunk_size == 0)
				return 1;
			break;
		case 'V':
			show_version();
			return 0;
		default:
			show_usage(stderr);
			return 1;
		}
	}

	argc -= toptind;
	argv += toptind;

	original_buf = xmalloc(chunk_size);
	compressed_buf = xmalloc(chunk_size - 1);
	decompressed_buf = xmalloc(chunk_size);
	compressor = alloc_compressor(chunk_size, compression_level);
	decompressor = alloc_decompressor();

	ret = -1;
	if (original_buf == NULL || compressed_buf == NULL ||
	    decompressed_buf == NULL ||
	    compressor == NULL || decompressor == NULL)
		goto out;

	if (argc == 0) {
		argv = default_file_list;
		argc = ARRAY_LEN(default_file_list);
	} else {
		for (i = 0; i < argc; i++)
			if (argv[i][0] == '-' && argv[i][1] == '\0')
				argv[i] = NULL;
	}

	printf("Benchmarking XPACK compression:\n");
	printf("\tChunk size: %"PRIu32"\n", chunk_size);
	printf("\tCompression level: %d\n", compression_level);

	for (i = 0; i < argc; i++) {
		struct file_stream in;

		ret = xopen_for_read(argv[i], &in);
		if (ret != 0)
			goto out;

		printf("Processing %"TS"...\n", in.name);

		ret = do_benchmark(&in, original_buf, compressed_buf,
				   decompressed_buf, chunk_size, compressor,
				   decompressor);
		xclose(&in);
		if (ret != 0)
			goto out;
	}
	ret = 0;
out:
	xpack_free_decompressor(decompressor);
	xpack_free_compressor(compressor);
	free(decompressed_buf);
	free(compressed_buf);
	free(original_buf);
	return -ret;
}
