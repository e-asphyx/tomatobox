#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

ssize_t _read(int fd, void *buf, size_t count) {
	errno = EBADF;
	return -1;
}

ssize_t _write(int fd, const void *buf, size_t count) {
	errno = EBADF;
	return -1;
}

int _close (int file) {
	errno = EBADF;
	return -1;
}

off_t _lseek(int fd, off_t offset, int whence) {
	errno = EINVAL;
	return  -1;
}
