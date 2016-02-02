#include "General.h"
#include "Helpers.h"

int ForkProcessForReading(const char** params)
{
    int pipes[NUM_PIPES][2];

    // pipes for parent to write and read
    pipe(pipes[PARENT_READ_PIPE]);
    pipe(pipes[PARENT_WRITE_PIPE]);

    int status = fork();

    if (status == 0)
    {
        dup2(CHILD_READ_FD(pipes), STDIN_FILENO);
        dup2(CHILD_WRITE_FD(pipes), STDOUT_FILENO);

        close(CHILD_READ_FD(pipes));
        close(CHILD_WRITE_FD(pipes));
        close(PARENT_READ_FD(pipes));
        close(PARENT_WRITE_FD(pipes));

        execv(params[0], (char* const*)params);
    }
    else if (status > 0)
    {
        close(CHILD_READ_FD(pipes));
        close(CHILD_WRITE_FD(pipes));

        close(PARENT_WRITE_FD(pipes));
    }
    else
        return -1;

    return PARENT_READ_FD(pipes);
}
