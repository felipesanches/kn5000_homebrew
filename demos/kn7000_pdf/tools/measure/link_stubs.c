/* link_stubs.c -- newlib's system calls and the directory functions, as nothing (only for measure_link.sh) */
void _exit(int c) { for (;;); }
int _write, _unlink, _stat, _sbrk, _read, _open, _lseek, _kill, _isatty, _gettimeofday, _close, _fstat, _getpid;
int _getentropy, ftruncate, __main, fz_open_directory, fz_is_directory, timegm;
