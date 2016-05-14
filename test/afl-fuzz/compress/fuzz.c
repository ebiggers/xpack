#include <assert.h>
#include <libxpack.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
	struct xpack_decompressor *d;
	struct xpack_compressor *c;
	int ret;
	int fd = open(argv[1], O_RDONLY);
	struct stat stbuf;
	assert(fd >= 0);
	ret = fstat(fd, &stbuf);
	assert(!ret);

	char in[stbuf.st_size];
	ret = read(fd, in, sizeof in);
	assert(ret == sizeof in);

	c = xpack_alloc_compressor(stbuf.st_size, 6);
	d = xpack_alloc_decompressor();

	char out[sizeof(in)];
	char checkarray[sizeof(in)];

	size_t csize = xpack_compress(c, in,sizeof in, out, sizeof out);
	if (csize) {
		enum decompress_result res;
		res = xpack_decompress(d, out, csize, checkarray, sizeof in, NULL);
		assert(!res);
		assert(!memcmp(in, checkarray, sizeof in));
	}

	xpack_free_compressor(c);
	xpack_free_decompressor(d);
	return 0;
}
