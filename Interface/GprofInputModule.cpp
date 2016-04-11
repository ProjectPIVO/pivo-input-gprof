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
#include "Gmon.h"
#include "GprofInputModule.h"
#include "Log.h"

void(*LogFunc)(int, const char*, ...) = nullptr;

extern "C"
{
    DLL_EXPORT_API InputModule* CreateInputModule()
    {
        return new GprofInputModule;
    }

    DLL_EXPORT_API void RegisterLogger(void(*log)(int, const char*, ...))
    {
        LogFunc = log;
    }
}

GprofInputModule::GprofInputModule()
{
    m_gmon = nullptr;
}

GprofInputModule::~GprofInputModule()
{
    //
}

const char* GprofInputModule::ReportName()
{
    return "gprof input module";
}

const char* GprofInputModule::ReportVersion()
{
    return "0.1-dev";
}

void GprofInputModule::ReportFeatures(IMF_SET &set)
{
    // nullify set
    IMF_CREATE(set);

    // flat profile is supported
    IMF_ADD(set, IMF_FLAT_PROFILE);

    // call graph is supported
    IMF_ADD(set, IMF_CALL_GRAPH);

    // using seconds as profiling unit
    IMF_ADD(set, IMF_USE_SECONDS);
}

bool GprofInputModule::LoadFile(const char* file, const char* binaryFile)
{
    // instantiate gmon file wrapper class
    m_gmon = GmonFile::Load(file, binaryFile);
    if (!m_gmon)
        return false;

    return true;
}

void GprofInputModule::GetClassTable(std::vector<ClassEntry> &dst)
{
    dst.clear();

    // TODO: implement this
}

void GprofInputModule::GetFunctionTable(std::vector<FunctionEntry> &dst)
{
    dst.clear();

    m_gmon->FillFunctionTable(dst);
}

void GprofInputModule::GetFlatProfileData(std::vector<FlatProfileRecord> &dst)
{
    dst.clear();

    m_gmon->FillFlatProfileTable(dst);
}

void GprofInputModule::GetCallGraphMap(CallGraphMap &dst)
{
    dst.clear();

    m_gmon->FillCallGraphMap(dst);
}

void GprofInputModule::GetCallTreeMap(CallTreeMap &dst)
{
    dst.clear();

    // Not supported by gmon format
}
