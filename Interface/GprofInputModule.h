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

    protected:
        //

    private:
        // gmon.out file wrapper class instance
        GmonFile* m_gmon;
};

#endif
