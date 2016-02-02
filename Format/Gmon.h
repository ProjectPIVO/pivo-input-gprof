#ifndef PIVO_GPROF_MODULE_GMON_H
#define PIVO_GPROF_MODULE_GMON_H

#include "UnitIdentifiers.h"

// gmon.out file magic cookie
#define	GMON_MAGIC "gmon"
// gmon.out highest supported file version
#define GMON_VERSION 1

// gmon.out file header
struct gmon_header
{
    char cookie[4];
    char version[4];
    char spare[3*4];
};

// recognized tags
enum GMON_Record_Tag
{
    GMON_TAG_TIME_HIST = 0,
    GMON_TAG_CG_ARC = 1,
    GMON_TAG_BB_COUNT = 2,
    MAX_GMON_REC_TYPE
};

// TODO: proper vma definition (depends on platform)
typedef uint64_t bfd_vma;

// Profiling unit definition
typedef unsigned char UNIT[2];

// histogram structure
struct histogram
{
    bfd_vma lowpc;
    bfd_vma highpc;
    uint32_t num_bins;
    int *sample;
};

// gmon.out file wrapper class
class GmonFile
{
    public:
        // public factory method loading data from supplied file
        static GmonFile* Load(const char* filename, const char* binaryFilename);

    private:
        // private constructor - use public factory method to instantiate this class
        GmonFile();

        // resolve symbols from executable file using builtin tools (nm, winnm, ..)
        void ResolveSymbols(const char* binaryFilename);

        // source file
        FILE* m_file;

        // read histogram record from file
        bool ReadHistogramRecord();
        // read call-graph record from file
        bool ReadCallGraphRecord();
        // read basic block record from file
        bool ReadBasicBlockRecord();

        // reads platform-dependent word (pointer) from file
        bool ReadVMA(bfd_vma *target);
        // reads 32-bit integer from file
        bool Read32(int32_t *target);
        // reads 64-bit integer from file
        bool Read64(int64_t *target);
        // reads specified count of bytes from file
        bool ReadBytes(void* target, int count);
        // reads string from file
        bool ReadString(std::string& target);

        // finds aligned histogram record from supplied PCs
        histogram* FindHistogram(bfd_vma lowpc, bfd_vma highpc);
        // clips histogram record to aligned block - lowpc equals highpc on success
        void ClipHistogramAddress(bfd_vma *lowpc, bfd_vma *highpc);

        // finds function entry using supplied address
        FunctionEntry* GetFunctionByAddress(uint64_t address);

        // header read from file
        gmon_header m_header;
        // converted version of gmon file
        uint32_t m_fileVersion;
        // tag counter
        uint64_t m_tagCount[MAX_GMON_REC_TYPE];

        // histogram storage
        std::list<histogram*> m_histograms;

        // stored histogram dimension
        std::string m_histDimension;
        // stored histogram dimension abbreviation
        char m_histDimensionAbbrev;
        // stored profiling rate
        uint32_t m_profRate;
        // stored histogram scale
        double m_histogramScale;

        // table of addresses of functions
        std::vector<FunctionEntry> m_functionTable;
};

#endif
