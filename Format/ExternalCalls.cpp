/**
 * Copyright (C) 2016 Martin Ubl <http://pivo.kennny.cz>
 *
 * This file is part of PIVO gprof input module.
 *
 * PIVO gprof input module is free software: you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * PIVO gprof input module is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PIVO gprof input module. If not,
 * see <http://www.gnu.org/licenses/>.
 **/

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
