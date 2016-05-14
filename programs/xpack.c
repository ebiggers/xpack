/*
 * xpack.c - a file compression and decompression program
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

#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#  include <sys/utime.h>
#else
#  include <sys/time.h>
#  include <unistd.h>
#  include <utime.h>
#endif

struct options {
	bool to_stdout;
	bool decompress;
	bool force;
	bool keep;
	int compression_level;
	u32 chunk_size;
	const tchar *suffix;
};

static const tchar *const optstring = T("123456789cdfhkL:s:S:V");

static void
show_usage(FILE *fp)
{
	fprintf(fp,
"Usage: %"TS" [-123456789cdfhkV] [-L LVL] [-s SIZE] [-S SUF] [FILE]...\n"
"Compress or decompress the specified FILEs.\n"
"\n"
"Options:\n"
"  -1        fastest (worst) compression\n"
"  -9        slowest (best) compression\n"
"  -c        write to standard output\n"
"  -d        decompress\n"
"  -f        overwrite existing output files\n"
"  -h        print this help\n"
"  -k        don't delete input files\n"
"  -L LVL    compression level [1-9] (default 6)\n"
"  -s SIZE   chunk size (default 524288)\n"
"  -S SUF    use suffix .SUF instead of .xpack\n"
"  -V        show version and legal information\n"
"\n"
"NOTICE: this program is currently experimental, and the on-disk format\n"
"is not yet stable!\n",
	program_invocation_name);
}

static void
show_version(void)
{
	printf(
"xpack compression program, experimental version\n"
"Copyright 2016 Eric Biggers\n"
"\n"
"This program is free software which may be modified and/or redistributed\n"
"under the terms of the MIT license.  There is NO WARRANTY, to the extent\n"
"permitted by law.  See the COPYING file for details.\n"
	);
}

/* Was the program invoked in decompression mode? */
static bool
is_xunpack(void)
{
	if (tstrxcmp(program_invocation_name, T("xunpack")) == 0)
		return true;
#ifdef _WIN32
	if (tstrxcmp(program_invocation_name, T("xunpack.exe")) == 0)
		return true;
#endif
	return false;
}

static const tchar *
get_suffix(const tchar *path, const tchar *suffix)
{
	const tchar *dot = tstrrchr(get_filename(path), '.');

	if (dot != NULL && tstrxcmp(dot + 1, suffix) == 0)
		return dot;
	return NULL;
}

static bool
has_suffix(const tchar *path, const tchar *suffix)
{
	return get_suffix(path, suffix) != NULL;
}

struct xpack_file_header {
#define XPACK_MAGIC "XPACK\0\0\0"
	char magic[8];
	u32 chunk_size;
	u16 header_size;
	u8 version;
	u8 compression_level;
};

struct xpack_chunk_header {
	u32 stored_size;
	u32 original_size;
};

static void
bswap_file_header(struct xpack_file_header *hdr)
{
	STATIC_ASSERT(sizeof(struct xpack_file_header) == 16);

	hdr->chunk_size = le32_bswap(hdr->chunk_size);
	hdr->header_size = le16_bswap(hdr->header_size);
}

static void
bswap_chunk_header(struct xpack_chunk_header *hdr)
{
	STATIC_ASSERT(sizeof(struct xpack_chunk_header) == 8);

	hdr->stored_size = le32_bswap(hdr->stored_size);
	hdr->original_size = le32_bswap(hdr->original_size);
}

static int
write_file_header(struct file_stream *out, u32 chunk_size, int compression_level)
{
	struct xpack_file_header hdr;

	memcpy(hdr.magic, XPACK_MAGIC, sizeof(hdr.magic));
	hdr.chunk_size = chunk_size;
	hdr.header_size = sizeof(hdr);
	hdr.version = 1;
	hdr.compression_level = compression_level;

	bswap_file_header(&hdr);
	return full_write(out, &hdr, sizeof(hdr));
}

static int
write_chunk_header(struct file_stream *out, u32 stored_size, u32 original_size)
{
	struct xpack_chunk_header hdr;

	hdr.stored_size = stored_size;
	hdr.original_size = original_size;

	bswap_chunk_header(&hdr);
	return full_write(out, &hdr, sizeof(hdr));
}

static int
do_compress(struct xpack_compressor *compressor, struct file_stream *in,
	    struct file_stream *out, u32 chunk_size)
{
	void *original_buf = xmalloc(chunk_size);
	void *compressed_buf = xmalloc(chunk_size - 1);
	ssize_t ret = -1;

	if (original_buf == NULL || compressed_buf == NULL)
		goto out;

	while ((ret = xread(in, original_buf, chunk_size)) > 0) {
		u32 original_size = ret;
		u32 compressed_size;
		void *stored_buf;
		u32 stored_size;

		compressed_size = xpack_compress(compressor,
						 original_buf,
						 original_size,
						 compressed_buf,
						 original_size - 1);
		if (compressed_size == 0) {
			/* Store the chunk uncompressed */
			stored_buf = original_buf;
			stored_size = original_size;
		} else {
			/* Store the chunk compressed */
			stored_buf = compressed_buf;
			stored_size = compressed_size;
		}

		ret = write_chunk_header(out, stored_size, original_size);
		if (ret != 0)
			goto out;

		ret = full_write(out, stored_buf, stored_size);
		if (ret != 0)
			goto out;
	}
out:
	free(compressed_buf);
	free(original_buf);
	return ret;
}

static int
do_decompress(struct xpack_decompressor *decompressor, struct file_stream *in,
	      struct file_stream *out, u32 chunk_size)
{
	void *original_buf = xmalloc(chunk_size);
	void *compressed_buf = xmalloc(chunk_size - 1);
	ssize_t ret = -1;
	struct xpack_chunk_header chunk_hdr;

	if (original_buf == NULL || compressed_buf == NULL)
		goto out;

	while ((ret = xread(in, &chunk_hdr, sizeof(chunk_hdr)))
			== sizeof(chunk_hdr))
	{
		u32 original_size;
		u32 stored_size;
		enum decompress_result result;

		bswap_chunk_header(&chunk_hdr);
		original_size = chunk_hdr.original_size;
		stored_size = chunk_hdr.stored_size;

		if (original_size < 1 || original_size > chunk_size ||
		    stored_size < 1 || stored_size > original_size) {
			msg("%"TS": file corrupt", in->name);
			ret = -1;
			goto out;
		}

		ret = xread(in, (stored_size == original_size) ?
				original_buf : compressed_buf, stored_size);
		if (ret < 0)
			goto out;

		if (ret != stored_size) {
			msg("%"TS": unexpected end-of-file", in->name);
			ret = -1;
			goto out;
		}

		if (stored_size != original_size) {
			/* Chunk was stored compressed */
			result = xpack_decompress(decompressor,
						  compressed_buf, stored_size,
						  original_buf, original_size,
						  NULL);
			if (result != DECOMPRESS_SUCCESS) {
				msg("%"TS": data corrupt", in->name);
				ret = -1;
				goto out;
			}
		}

		ret = full_write(out, original_buf, original_size);
		if (ret != 0)
			goto out;
	}
	if (ret > 0) {
		msg("%"TS": unexpected end-of-file", in->name);
		ret = -1;
	}
out:
	free(compressed_buf);
	free(original_buf);
	return ret;
}

static int
stat_file(struct file_stream *in, struct stat *stbuf, bool allow_hard_links)
{
	if (fstat(in->fd, stbuf) != 0) {
		msg("%"TS": unable to stat file", in->name);
		return -1;
	}

	if (!S_ISREG(stbuf->st_mode) && !in->is_standard_stream) {
		msg("%"TS" is %s -- skipping",
		    in->name, S_ISDIR(stbuf->st_mode) ? "a directory" :
							"not a regular file");
		return -2;
	}

	if (stbuf->st_nlink > 1 && !allow_hard_links) {
		msg("%"TS" has multiple hard links -- skipping "
		    "(use -f to process anyway)", in->name);
		return -2;
	}

	return 0;
}

static void
restore_mode(struct file_stream *out, const struct stat *stbuf)
{
#ifndef _WIN32
	if (fchmod(out->fd, stbuf->st_mode) != 0)
		msg_errno("%"TS": unable to preserve mode", out->name);
#endif
}

static void
restore_owner_and_group(struct file_stream *out, const struct stat *stbuf)
{
#ifndef _WIN32
	if (fchown(out->fd, stbuf->st_uid, stbuf->st_gid) != 0) {
		msg_errno("%"TS": unable to preserve owner and group",
			  out->name);
	}
#endif
}

static void
restore_timestamps(struct file_stream *out, const tchar *newpath,
		   const struct stat *stbuf)
{
	int ret;
#if defined(HAVE_FUTIMENS)
	struct timespec times[2] = {
		stbuf->st_atim, stbuf->st_mtim,
	};
	ret = futimens(out->fd, times);
#elif defined(HAVE_FUTIMES)
	struct timeval times[2] = {
		{ stbuf->st_atim.tv_sec, stbuf->st_atim.tv_nsec / 1000, },
		{ stbuf->st_mtim.tv_sec, stbuf->st_mtim.tv_nsec / 1000, },
	};
	ret = futimes(out->fd, times);
#else /* HAVE_FUTIMES */
	struct tutimbuf times = {
		stbuf->st_atime, stbuf->st_mtime,
	};
	ret = tutime(newpath, &times);
#endif /* !HAVE_FUTIMES */
	if (ret != 0)
		msg_errno("%"TS": unable to preserve timestamps", out->name);
}

static void
restore_metadata(struct file_stream *out, const tchar *newpath,
		 const struct stat *stbuf)
{
	restore_mode(out, stbuf);
	restore_owner_and_group(out, stbuf);
	restore_timestamps(out, newpath, stbuf);
}

static int
decompress_file(struct xpack_decompressor *decompressor, const tchar *path,
		const struct options *options)
{
	tchar *newpath = NULL;
	struct file_stream in;
	struct file_stream out;
	struct xpack_file_header hdr;
	struct stat stbuf;
	int ret;
	int ret2;

	if (path != NULL && !options->to_stdout) {
		const tchar *suffix = get_suffix(path, options->suffix);
		if (suffix == NULL) {
			msg("\"%"TS"\" does not end with the .%"TS" suffix -- "
			    "skipping", path, options->suffix);
			ret = -2;
			goto out;
		}
		newpath = xmalloc((suffix - path + 1) * sizeof(tchar));
		tmemcpy(newpath, path, suffix - path);
		newpath[suffix - path] = '\0';
	}

	ret = xopen_for_read(path, &in);
	if (ret != 0)
		goto out_free_newpath;

	if (!options->force && isatty(in.fd)) {
		msg("Refusing to read compressed data from terminal.  "
		    "Use -f to override.\nFor help, use -h.");
		ret = -1;
		goto out_close_in;
	}

	ret = stat_file(&in, &stbuf, options->force || newpath == NULL);
	if (ret != 0)
		goto out_close_in;

	ret = xread(&in, &hdr, sizeof(hdr));
	if (ret < 0)
		goto out_close_in;
	if (ret != sizeof(hdr)) {
		msg("%"TS": not in XPACK format", in.name);
		ret = -1;
		goto out_close_in;
	}
	bswap_file_header(&hdr);

	if (memcmp(hdr.magic, XPACK_MAGIC, sizeof(hdr.magic)) != 0) {
		msg("%"TS": not in XPACK format", in.name);
		ret = -1;
		goto out_close_in;
	}

	if (hdr.version != 1) {
		msg("%"TS": unsupported version (%d)", in.name, hdr.version);
		ret = -1;
		goto out_close_in;
	}

	if (hdr.header_size < sizeof(hdr)) {
		msg("%"TS": incorrect header size (%"PRIu16")", in.name,
		    hdr.header_size);
		ret = -1;
		goto out_close_in;
	}

	if (hdr.chunk_size < 1024 || hdr.chunk_size > 67108864) {
		msg("%"TS": unsupported chunk size (%"PRIu32")", in.name,
		    hdr.chunk_size);
		ret = -1;
		goto out_close_in;
	}

	ret = skip_bytes(&in, hdr.header_size - sizeof(hdr));
	if (ret != 0)
		goto out_close_in;

	ret = xopen_for_write(newpath, options->force, &out);
	if (ret != 0)
		goto out_close_in;

	ret = do_decompress(decompressor, &in, &out, hdr.chunk_size);
	if (ret != 0)
		goto out_close_out;

	if (path != NULL && newpath != NULL)
		restore_metadata(&out, newpath, &stbuf);
	ret = 0;
out_close_out:
	ret2 = xclose(&out);
	if (ret == 0)
		ret = ret2;
	if (ret != 0 && newpath != NULL)
		tunlink(newpath);
out_close_in:
	xclose(&in);
	if (ret == 0 && path != NULL && newpath != NULL && !options->keep)
		tunlink(path);
out_free_newpath:
	free(newpath);
out:
	return ret;
}

static int
compress_file(struct xpack_compressor *compressor, const tchar *path,
	      const struct options *options)
{
	tchar *newpath = NULL;
	struct file_stream in;
	struct file_stream out;
	struct stat stbuf;
	int ret;
	int ret2;

	if (path != NULL && !options->to_stdout) {
		size_t path_nchars, suffix_nchars;

		if (!options->force && has_suffix(path, options->suffix)) {
			msg("%"TS": already has .%"TS" suffix -- skipping",
			    path, options->suffix);
			ret = -2;
			goto out;
		}
		path_nchars = tstrlen(path);
		suffix_nchars = tstrlen(options->suffix);
		newpath = xmalloc((path_nchars + 1 + suffix_nchars + 1) *
					sizeof(tchar));
		tmemcpy(newpath, path, path_nchars);
		newpath[path_nchars] = '.';
		tmemcpy(&newpath[path_nchars + 1], options->suffix,
			suffix_nchars + 1);
	}

	ret = xopen_for_read(path, &in);
	if (ret != 0)
		goto out_free_newpath;

	ret = stat_file(&in, &stbuf, options->force || newpath == NULL);
	if (ret != 0)
		goto out_close_in;

	ret = xopen_for_write(newpath, options->force, &out);
	if (ret != 0)
		goto out_close_in;

	if (!options->force && isatty(out.fd)) {
		msg("Refusing to write compressed data to terminal. "
		    "Use -f to override.\nFor help, use -h.");
		ret = -1;
		goto out_close_out;
	}

	ret = write_file_header(&out, options->chunk_size,
				options->compression_level);
	if (ret != 0)
		goto out_close_out;

	ret = do_compress(compressor, &in, &out, options->chunk_size);
	if (ret != 0)
		goto out_close_out;

	if (path != NULL && newpath != NULL)
		restore_metadata(&out, newpath, &stbuf);
	ret = 0;
out_close_out:
	ret2 = xclose(&out);
	if (ret == 0)
		ret = ret2;
	if (ret != 0 && newpath != NULL)
		tunlink(newpath);
out_close_in:
	xclose(&in);
	if (ret == 0 && path != NULL && newpath != NULL && !options->keep)
		tunlink(path);
out_free_newpath:
	free(newpath);
out:
	return ret;
}

int
tmain(int argc, tchar *argv[])
{
	struct options options;
	tchar *default_file_list[] = { NULL };
	int opt_char;
	int i;
	int ret;

	program_invocation_name = get_filename(argv[0]);

	options.to_stdout = false;
	options.decompress = is_xunpack();
	options.force = false;
	options.keep = false;
	options.compression_level = 6;
	options.chunk_size = 524288;
	options.suffix = T("xpack");

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
			options.compression_level = opt_char - '0';
			break;
		case 'c':
			options.to_stdout = true;
			break;
		case 'd':
			options.decompress = true;
			break;
		case 'f':
			options.force = true;
			break;
		case 'h':
			show_usage(stdout);
			return 0;
		case 'k':
			options.keep = true;
			break;
		case 'L':
			options.compression_level =
				parse_compression_level(toptarg);
			if (options.compression_level <= 0)
				return 1;
			break;
		case 's':
			options.chunk_size = parse_chunk_size(toptarg);
			if (options.chunk_size == 0)
				return 1;
			break;
		case 'S':
			options.suffix = toptarg;
			break;
		case 'V':
			show_version();
			return 0;
		default:
			show_usage(stderr);
			return 1;
		}
	}

	argv += toptind;
	argc -= toptind;

	if (argc == 0) {
		argv = default_file_list;
		argc = ARRAY_LEN(default_file_list);
	} else {
		for (i = 0; i < argc; i++)
			if (argv[i][0] == '-' && argv[i][1] == '\0')
				argv[i] = NULL;
	}

	ret = 0;
	if (options.decompress) {
		struct xpack_decompressor *d;

		d = alloc_decompressor();
		if (d == NULL)
			return 1;

		for (i = 0; i < argc; i++)
			ret |= -decompress_file(d, argv[i], &options);

		xpack_free_decompressor(d);
	} else {
		struct xpack_compressor *c;

		c = alloc_compressor(options.chunk_size,
				     options.compression_level);
		if (c == NULL)
			return 1;

		for (i = 0; i < argc; i++)
			ret |= -compress_file(c, argv[i], &options);

		xpack_free_compressor(c);
	}

	/*
	 * If ret=0, there were no warnings or errors.  Exit with status 0.
	 * If ret=2, there was at least one warning.  Exit with status 2.
	 * Else, there was at least one error.  Exit with status 1.
	 */
	if (ret != 0 && ret != 2)
		ret = 1;

	return ret;
}
