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
