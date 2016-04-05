#include "General.h"
#include "Helpers.h"
#include "Gmon.h"
#include "GprofInputModule.h"
#include "Log.h"
#include "../config_gprof.h"

#include <algorithm>
#include <math.h>

GmonFile::GmonFile()
{
    for (int i = 0; i < MAX_GMON_REC_TYPE; i++)
        m_tagCount[i] = 0;
}

GmonFile* GmonFile::Load(const char* filename, const char* binaryFilename)
{
    LogFunc(LOG_VERBOSE, "Loading gmon file %s", filename);

    // open file
    FILE* gf = fopen(filename, "rb");
    if (!gf)
    {
        LogFunc(LOG_ERROR, "Couldn't find gmon file %s", filename);
        return nullptr;
    }

    FILE* tmpbf = fopen(binaryFilename, "rb");
    if (!tmpbf)
        LogFunc(LOG_ERROR, "Invalid binary file %s supplied, won't be possible to resolve symbols!", binaryFilename);
    else
        fclose(tmpbf);

    GmonFile* gmon = new GmonFile();

    gmon->m_file = gf;

    LogFunc(LOG_VERBOSE, "Reading gmon file header");

    // read raw header
    if (fread(&gmon->m_header, sizeof(gmon_header), 1, gmon->m_file) != 1)
    {
        LogFunc(LOG_ERROR, "File does not contain valid gmon header");
        fclose(gmon->m_file);
        delete gmon;
        return nullptr;
    }

    // verify magic cookie
    if (strncmp(gmon->m_header.cookie, GMON_MAGIC, 4) != 0)
    {
        LogFunc(LOG_ERROR, "File does not contain valid gmon magic cookie");
        fclose(gmon->m_file);
        delete gmon;
        return nullptr;
    }

    // TODO: platform-dependent endianity
    // convert character-based version to integer as-is
    gmon->m_fileVersion = *((uint32_t*)gmon->m_header.version);

    // TODO: verify supported file version ( <= GMON_VERSION ) - TODO: verify version numbering and compatibility

    gmon->ResolveSymbols(binaryFilename);

    uint8_t tag;

    // read all available records - read tag, and then call appropriate method reading the record
    while (fread(&tag, sizeof(tag), 1, gmon->m_file) == 1)
    {
        switch (tag)
        {
            // histogram record
            case GMON_TAG_TIME_HIST:
                LogFunc(LOG_DEBUG, "Reading histogram record");
                gmon->ReadHistogramRecord();
                break;
            // call-graph record
            case GMON_TAG_CG_ARC:
                LogFunc(LOG_DEBUG, "Reading call-graph record");
                gmon->ReadCallGraphRecord();
                break;
            // basic block record
            case GMON_TAG_BB_COUNT:
                LogFunc(LOG_DEBUG, "Reading basic block record");
                gmon->ReadBasicBlockRecord();
                break;
            // anything else is considered an error
            default:
                LogFunc(LOG_ERROR, "File contains invalid tag: %i", tag);
                fclose(gmon->m_file);
                delete gmon;
                return nullptr;
        }
    }

    // cleanup
    fclose(gmon->m_file);

    // report record counts to log
    LogFunc(LOG_VERBOSE, "gmon file loaded, %llu histogram records, %llu call-graph records, %llu basic block records",
        gmon->m_tagCount[GMON_TAG_TIME_HIST], gmon->m_tagCount[GMON_TAG_CG_ARC], gmon->m_tagCount[GMON_TAG_BB_COUNT]);

    // perform scaling of function entries
    gmon->ScaleAndAlignEntries();

    gmon->ProcessFlatProfile();

    gmon->ProcessCallGraph();

    return gmon;
}

void GmonFile::ResolveSymbols(const char* binaryFilename)
{
    // build nm binary call parameters
    const char *argv[] = {NM_BINARY_PATH, "-a", "-C", binaryFilename, 0};

    LogFunc(LOG_VERBOSE, "Reasolving symbols using application binary");

    int readfd = ForkProcessForReading(argv);

    if (readfd <= 0)
    {
        LogFunc(LOG_ERROR, "Could not execute nm binary for symbol resolving, no symbols loaded");
        return;
    }

    // buffer for reading lines from nm stdout
    char buffer[256];

    // Read from child’s stdout
    int res, pos, cnt;
    char c;
    uint64_t laddr;
    char* endptr;
    char fncType;

    cnt = 0;

    // line reading loop - terminated by file end
    while (true)
    {
        pos = 0;
        while ((res = read(readfd, &c, sizeof(char))) == 1)
        {
            // stop reading line when end of line character is acquired
            if (c == 10 || c == 13)
                break;

            // read only 255 characters, strip the rest
            if (pos < 255)
                buffer[pos++] = c;
        }

        // this both means end of file - eighter no character was read, or we reached zero character
        if (res <= 0 || c == 0)
            break;

        // properly null-perminate string
        buffer[pos] = 0;

        // require some minimal length, parsing would fail anyway
        if (strlen(buffer) < 8)
            continue;

        // parse address
        laddr = strtoull(buffer, &endptr, 16);
        if (endptr - buffer + 2 > pos)
            break;

        // resolve function type
        fncType = *(endptr+1);
        if (fncType == 'T' || fncType == 't')
            fncType = FET_TEXT;
        else
            fncType = FET_MISC;

        // store "the rest of line" as function name to function table
        m_functionTable.push_back({ laddr, 0, endptr+3, NO_CLASS, (FunctionEntryType)fncType });
        cnt++;

        // This logging call usually fills console with loads of messages; commented out for sanity reasons
        //LogFunc(LOG_VERBOSE, "Address: %llu, function: %s", laddr, m_functionTable[laddr].c_str());
    }

    close(readfd);

    // sort function entries to allow effective search
    std::sort(m_functionTable.begin(), m_functionTable.end(), FunctionEntrySortPredicate());

    LogFunc(LOG_VERBOSE, "Loaded %i symbols from supplied binary file", cnt);
}

FunctionEntry* GmonFile::GetFunctionByAddress(uint64_t address, uint32_t* functionIndex, bool useScaled)
{
    if (functionIndex)
        *functionIndex = 0;

    if (m_functionTable.empty())
        return nullptr;

    // we assume that m_functionTable is sorted from lower address to higher
    // so we are able to perform binary search in O(log(n)) complexity

    uint64_t ilow, ihigh, imid;

    ilow = 0;
    ihigh = m_functionTable.size() - 1;

    imid = (ilow + ihigh) / 2;

    // iterative binary search
    while (ilow <= ihigh)
    {
        if ((useScaled ? m_functionTable[imid].scaled_address : m_functionTable[imid].address) > address)
            ihigh = imid - 1;
        else
            ilow = imid + 1;

        imid = (ilow + ihigh) / 2;
    }

    // the outcome may be one step higher (depending from which side we arrived), than we would like to have - we are looking for
    // "highest lower address", i.e. for addresses 2, 5, 10, and input address 7, we return entry with address 5

    if ((useScaled ? m_functionTable[ilow].scaled_address : m_functionTable[ilow].address) <= address)
    {
        if (functionIndex)
            *functionIndex = ilow;
        return &m_functionTable[ilow];
    }

    if (functionIndex)
        *functionIndex = ilow - 1;

    return &m_functionTable[ilow - 1];
}

void GmonFile::GetFunctionListByAddressRange(uint64_t lowpc, uint64_t highpc, std::list<uint32_t>* indexList, bool useScaled)
{
    if (lowpc > highpc)
        return;

    if (!indexList)
        return;

    indexList->clear();

    uint32_t ind;
    FunctionEntry* fe;

    fe = GetFunctionByAddress(lowpc, &ind, useScaled);

    while (fe && ((!useScaled && fe->address < highpc) || (useScaled && fe->scaled_address < highpc)))
    {
        indexList->push_back(ind);

        ++ind;

        if (ind == m_functionTable.size())
            fe = nullptr;
        else
            fe = &m_functionTable[ind];
    }
}

void GmonFile::ScaleAndAlignEntries()
{
    // This method is now used just for aligning function entries to "measurable scale",
    // no more functionality for now

    LogFunc(LOG_VERBOSE, "Scaling and aligning function entries");

    int i;

    for (i = 0; i < m_functionTable.size(); i++)
    {
        // scale address by profiling unit
        m_functionTable[i].scaled_address = m_functionTable[i].address / sizeof(UNIT);
    }
}

void GmonFile::AssignHistogramEntries(histogram* hist)
{
    LogFunc(LOG_DEBUG, "Assigning histogram entries for 0x%.16llX - 0x%.16llX", hist->lowpc, hist->highpc);

    uint32_t index;
    bfd_vma bin_low, bin_high, sym_low, sym_high, overlap, hist_base_pc;

    double time, total_time, credit;

    std::list<uint32_t> indexList;

    hist_base_pc = (hist->lowpc / sizeof(UNIT));

    // go through all bins present in this histogram record
    for (int i = 0; i < hist->num_bins; i++)
    {
        if (hist->sample[i] <= 0)
            continue;

        // calculate low and high address
        bin_low = hist_base_pc + (bfd_vma)(m_histogramScale * i);
        bin_high = hist_base_pc + (bfd_vma)(m_histogramScale * (i + 1));

        time = hist->sample[i];
        total_time += time;

        // retrieve all functions, that are present in this bin
        GetFunctionListByAddressRange(bin_low, bin_high, &indexList, true);
        for (std::list<uint32_t>::iterator itr = indexList.begin(); itr != indexList.end(); ++itr)
        {
            index = *itr;

            // calculate low and high address of this function
            sym_low = m_functionTable[index].scaled_address;
            sym_high = m_functionTable[index + 1].scaled_address;

            // calculate, how much of the bin is covered by this function
            // functions may overlap in bins
            overlap = nmin(bin_high, sym_high) - nmax(bin_low, sym_low);
            if (overlap > 0)
            {
                // this is the real "time credit" for this function call
                credit = overlap * time / m_histogramScale;

                // TODO: implement symbol table exclusion (i.e. builtins)

                m_flatProfile[index].timeTotal += credit;
            }
        }
    }
}

void GmonFile::ProcessFlatProfile()
{
    LogFunc(LOG_VERBOSE, "Processing flat profile");

    m_flatProfile.resize(m_functionTable.size());

    FlatProfileRecord *fp;

    // prepare flat profile table, it will match function table at first stage of filling
    for (int i = 0; i < m_functionTable.size(); i++)
    {
        fp = &m_flatProfile[i];

        fp->functionId = i;
        fp->callCount = 0;
        fp->timeTotal = 0;
        fp->timeTotalPct = 0.0f;
    }

    for (std::list<histogram*>::iterator itr = m_histograms.begin(); itr != m_histograms.end(); ++itr)
        AssignHistogramEntries(*itr);

    // scale profiling entries using profiling rate
    // profiling rate tells us how many measures are in one reported unit
    double profRate = (double)m_profRate;
    for (int i = 0; i < m_flatProfile.size(); i++)
        m_flatProfile[i].timeTotal /= profRate;

    uint32_t fi;
    callgraph_arc* cg;

    // go through all callgraph data and collect call counts using so called "arcs"
    for (std::list<callgraph_arc*>::iterator itr = m_callGraphArcs.begin(); itr != m_callGraphArcs.end(); ++itr)
    {
        cg = *itr;

        // also find function, add call count gathered by gprof
        FunctionEntry *fe = GetFunctionByAddress(cg->selfpc, &fi);
        if (fe)
            m_flatProfile[fi].callCount += cg->count;
    }
}

bool GmonFile::ReadVMA(bfd_vma *target)
{
    // TODO: platform dependent disambiguation

    if (fread(target, sizeof(bfd_vma), 1, m_file) != 1)
        return false;

    return true;
}

bool GmonFile::Read32(int32_t *target)
{
    if (fread(target, sizeof(int32_t), 1, m_file) != 1)
        return false;

    return true;
}

bool GmonFile::Read64(int64_t *target)
{
    if (fread(target, sizeof(int64_t), 1, m_file) != 1)
        return false;

    return true;
}

bool GmonFile::ReadBytes(void* target, int count)
{
    if (fread(target, sizeof(char), count, m_file) != count)
        return false;

    return true;
}

bool GmonFile::ReadString(std::string& target)
{
    char c;
    target.clear();

    // read until we reach zero
    while (fread(&c, sizeof(char), 1, m_file) == 1)
    {
        // when zero is found, return success
        if (c == '\0')
            return true;

        // append character to string
        target += c;
    }

    return false;
}

bool GmonFile::ReadHistogramRecord()
{
    histogram *n_record, *record;

    unsigned int profrate;
    char n_hist_dimension[15];
    char n_hist_dimension_abbrev;
    double n_hist_scale;

    n_record = new histogram;

    // read header, field by field
    if (!ReadVMA(&n_record->lowpc)
        || !ReadVMA(&n_record->highpc)
        || !Read32((int32_t*)&n_record->num_bins)
        || !Read32((int32_t*)&profrate)
        || !ReadBytes(n_hist_dimension, 15)
        || !ReadBytes(&n_hist_dimension_abbrev, 1))
    {
        LogFunc(LOG_ERROR, "gmon file does not contain valid header");
        return false;
    }

    // count histogram scale
    n_hist_scale = (double)((n_record->highpc - n_record->lowpc) / sizeof(UNIT)) / n_record->num_bins;

    // if we are reading first record, just store information
    if (m_tagCount[GMON_TAG_TIME_HIST] == 0)
    {
        m_profRate = profrate;
        m_histDimension = n_hist_dimension;
        m_histDimensionAbbrev = n_hist_dimension_abbrev;
        m_histogramScale = n_hist_scale;
    }
    else // otherwise check, if something went wrong about granularity or sampling dimension
    {
        // check dimension change
        if (strncmp(m_histDimension.c_str(), n_hist_dimension, 15) != 0)
        {
            LogFunc(LOG_ERROR, "Dimension unit changed between histogram records from %s to %s", m_histDimension.c_str(), n_hist_dimension);
            return false;
        }

        // check abbreviation change (although this should not change until the dimension changes as well)
        if (m_histDimensionAbbrev != n_hist_dimension_abbrev)
        {
            LogFunc(LOG_ERROR, "Dimension unit abbreviation changed between histogram records from %c to %c", m_histDimensionAbbrev, n_hist_dimension_abbrev);
            return false;
        }

        // verify the scale didn't change
        if (fabs(m_histogramScale - n_hist_scale) < 0.00001)
        {
            LogFunc(LOG_ERROR, "Histogram scale changed between histogram records from %lf to %lf", m_histogramScale, n_hist_scale);
            return false;
        }
    }

    histogram* existing;

    // find histogram, if exist for this part of program
    if ((existing = FindHistogram(n_record->lowpc, n_record->highpc)) != nullptr)
    {
        record = existing;
        delete n_record;
    }
    else // otherwise create new
    {
        bfd_vma lowpc, highpc;

        lowpc = n_record->lowpc;
        highpc = n_record->highpc;

        ClipHistogramAddress(&lowpc, &highpc);
        if (lowpc != highpc)
        {
            LogFunc(LOG_ERROR, "Found overlapping histogram records");
            return false;
        }

        m_histograms.push_back(n_record);
        record = n_record;

        record->sample = new int[record->num_bins];
        memset(record->sample, 0, sizeof(int)*record->num_bins);
    }

    // read samples, add them to sample fields
    for (uint32_t i = 0; i < record->num_bins; i++)
    {
        UNIT count;
        if (fread(&count[0], sizeof(count), 1, m_file) != 1)
        {
            LogFunc(LOG_ERROR, "Error while reading samples from gmon file - unexpected end of file");
            return false;
        }

        // TODO: endianity

        // add to appropriate field
        record->sample[i] += *((uint16_t*)&count);
    }

    m_tagCount[GMON_TAG_TIME_HIST]++;
    return true;
}

void GmonFile::ClipHistogramAddress(bfd_vma *lowpc, bfd_vma *highpc)
{
    bool found = false;

    // if there are no other histogram records, just align two PCs and mark it successful
    if (m_tagCount[GMON_TAG_TIME_HIST] == 0)
    {
        *highpc = *lowpc;
        return;
    }

    histogram* tmp;

    // go through all histogram records
    for (std::list<histogram*>::iterator itr = m_histograms.begin(); itr != m_histograms.end(); ++itr)
    {
        tmp = *itr;

        // compute common low and high PC
        bfd_vma common_low, common_high;
        common_low = nmax(tmp->lowpc, *lowpc);
        common_high = nmin(tmp->highpc, *highpc);

        if (common_low < common_high)
        {
            if (found)
            {
                LogFunc(LOG_ERROR, "Overlapping histogram records!");
                *highpc = *lowpc; // this is wrong, maybe
                return;
            }

            found = true;
            *lowpc = common_low;
            *highpc = common_high;
        }
    }

    if (!found)
        *highpc = *lowpc;
}

histogram* GmonFile::FindHistogram(bfd_vma lowpc, bfd_vma highpc)
{
    // go through all histogram records, and find matching aligned histogram
    for (std::list<histogram*>::iterator itr = m_histograms.begin(); itr != m_histograms.end(); ++itr)
    {
        if ((*itr)->lowpc == lowpc && (*itr)->highpc == highpc)
            return *itr;
    }

    return nullptr;
}

void GmonFile::ProcessCallGraph()
{
    LogFunc(LOG_VERBOSE, "Processing call graph");

    uint32_t srcIndex, dstIndex;
    callgraph_arc* arc;

    m_callGraph.clear();

    // go through all callgraph arc collected from gmon file and assign function entry (index) to them

    for (std::list<callgraph_arc*>::iterator itr = m_callGraphArcs.begin(); itr != m_callGraphArcs.end(); itr++)
    {
        arc = *itr;

        if (!GetFunctionByAddress(arc->frompc, &srcIndex, false))
        {
            LogFunc(LOG_WARNING, "No function containing caller address %llu found, ignoring", arc->frompc);
            continue;
        }

        if (!GetFunctionByAddress(arc->selfpc, &dstIndex, false))
        {
            LogFunc(LOG_WARNING, "No function containing callee address %llu found, ignoring", arc->selfpc);
            continue;
        }

        // call graph arcs may contain multiple caller-callee entries for same function pair,
        // i.e. when the callee is called from multiple locations within caller function
        // therefore we add values instead of assigning

        if (m_callGraph.find(srcIndex) == m_callGraph.end() ||
            m_callGraph[srcIndex].find(dstIndex) == m_callGraph[srcIndex].end())
        {
            m_callGraph[srcIndex][dstIndex] = 0;
        }

        m_callGraph[srcIndex][dstIndex] += arc->count;
    }
}

bool GmonFile::ReadCallGraphRecord()
{
    callgraph_arc *cg = new callgraph_arc();

    // read call graph record - source PC, self PC and count
    if (!ReadVMA(&cg->frompc)
        || !ReadVMA(&cg->selfpc)
        || !Read32((int32_t*)&cg->count))
    {
        delete cg;
        LogFunc(LOG_ERROR, "Unexpected end of file while reading callgraph record");
        return false;
    }

    LogFunc(LOG_DEBUG, "Read call graph block, frompc %llu, selfpc %llu, count %lu", cg->frompc, cg->selfpc, cg->count);

    // just store recorded data for later reuse
    m_callGraphArcs.push_back(cg);

    m_tagCount[GMON_TAG_CG_ARC]++;
    return true;
}

bool GmonFile::ReadBasicBlockRecord()
{
    uint32_t nblocks;
    std::string tmp;
    bfd_vma addr, ncalls;
    uint32_t line_num;

    // read block count
    if (!Read32((int32_t*)&nblocks))
    {
        LogFunc(LOG_ERROR, "Unexpected end of file while reading basic block record");
        return false;
    }

    // old version contained status string
    if (m_fileVersion == 0)
        ReadString(tmp);

    // read all available blocks
    for (uint32_t i = 0; i < nblocks; i++)
    {
        // old version contained lots of fields we don't care about now
        if (m_fileVersion == 0)
        {
            if (!ReadVMA(&ncalls)
                || !ReadVMA(&addr)
                || !ReadString(tmp) // deprecated
                || !ReadString(tmp) // deprecated
                || !Read32((int32_t*)&line_num))
            {
                LogFunc(LOG_ERROR, "Unexpected end of file while reading basic block record data");
                return false;
            }
        }
        else
        {
            if (!ReadVMA(&addr)
                || !ReadVMA(&ncalls))
            {
                LogFunc(LOG_ERROR, "Unexpected end of file while reading basic block record data");
                return false;
            }
        }

        // TODO: store data and deduce some useful information
    }

    m_tagCount[GMON_TAG_BB_COUNT]++;
    return true;
}

void GmonFile::FillFunctionTable(std::vector<FunctionEntry> &dst)
{
    LogFunc(LOG_VERBOSE, "Passing function table from input module to core");

    dst.assign(m_functionTable.begin(), m_functionTable.end());
}

void GmonFile::FillFlatProfileTable(std::vector<FlatProfileRecord> &dst)
{
    LogFunc(LOG_VERBOSE, "Passing flat profile table from input module to core");

    dst.assign(m_flatProfile.begin(), m_flatProfile.end());
}

void GmonFile::FillCallGraphMap(CallGraphMap &dst)
{
    LogFunc(LOG_VERBOSE, "Passing call graph from input module to core");

    // perform deep copy
    for (CallGraphMap::iterator itr = m_callGraph.begin(); itr != m_callGraph.end(); ++itr)
        for (std::map<uint32_t, uint64_t>::iterator sitr = itr->second.begin(); sitr != itr->second.end(); ++sitr)
            dst[itr->first][sitr->first] = sitr->second;
}
