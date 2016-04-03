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
