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

void GprofInputModule::ReportFeatures(IMF_SET &set)
{
    // nullify set
    IMF_CREATE(set);

    // for now, no support
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

    // TODO: implement this
}
