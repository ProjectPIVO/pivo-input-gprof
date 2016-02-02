#ifndef PIVO_GPROF_MODULE_HELPERS_H
#define PIVO_GPROF_MODULE_HELPERS_H

#define NUM_PIPES          2
#define PARENT_WRITE_PIPE  0
#define PARENT_READ_PIPE   1

#define READ_FD  0
#define WRITE_FD 1
#define PARENT_READ_FD(p)  ( p[PARENT_READ_PIPE][READ_FD]   )
#define PARENT_WRITE_FD(p) ( p[PARENT_WRITE_PIPE][WRITE_FD] )
#define CHILD_READ_FD(p)   ( p[PARENT_WRITE_PIPE][READ_FD]  )
#define CHILD_WRITE_FD(p)  ( p[PARENT_READ_PIPE][WRITE_FD]  )

// forks process, executes first parameter in params array, returns file descriptor for reading
int ForkProcessForReading(const char** params);

#endif
