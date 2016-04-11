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

#ifndef PIVO_GPROF_MODULE_H
#define PIVO_GPROF_MODULE_H

#include "InputModule.h"
#include "InputModuleFeatures.h"

extern void(*LogFunc)(int, const char*, ...);

// gprof input module for PIVO suite
class GprofInputModule : public InputModule
{
    public:
        GprofInputModule();
        ~GprofInputModule();

        virtual const char* ReportName();
        virtual const char* ReportVersion();
        virtual void ReportFeatures(IMF_SET &set);
        virtual bool LoadFile(const char* file, const char* binaryFile);
        virtual void GetClassTable(std::vector<ClassEntry> &dst);
        virtual void GetFunctionTable(std::vector<FunctionEntry> &dst);
        virtual void GetFlatProfileData(std::vector<FlatProfileRecord> &dst);
        virtual void GetCallGraphMap(CallGraphMap &dst);
        virtual void GetCallTreeMap(CallTreeMap &dst);

    protected:
        //

    private:
        // gmon.out file wrapper class instance
        GmonFile* m_gmon;
};

#endif
