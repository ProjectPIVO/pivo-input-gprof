#include "General.h"
#include "Helpers.h"
#include "Gmon.h"
#include "GprofInputModule.h"
#include "Log.h"

#include <algorithm>
#include <math.h>

GmonFile::GmonFile()
{
    for (int i = 0; i < MAX_GMON_REC_TYPE; i++)
        m_tagCount[i] = 0;
}

GmonFile* GmonFile::Load(const char* filename, const char* binaryFilename)
{
    LogFunc(LOG_DEBUG, "Loading gmon file %s", filename);

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
                LogFunc(LOG_VERBOSE, "Reading histogram record");
                gmon->ReadHistogramRecord();
                break;
            // call-graph record
            case GMON_TAG_CG_ARC:
                LogFunc(LOG_VERBOSE, "Reading call-graph record");
                gmon->ReadCallGraphRecord();
                break;
            // basic block record
            case GMON_TAG_BB_COUNT:
                LogFunc(LOG_VERBOSE, "Reading basic block record");
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

    gmon->ProcessFlatProfile();

    return gmon;
}

void GmonFile::ResolveSymbols(const char* binaryFilename)
{
    // binary name is hardcoded for now
    // TODO: more portable way of resolving nm executable path
    const char *argv[] = {"/usr/bin/nm", "-C", binaryFilename, 0};

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
        laddr = strtol(buffer, &endptr, 16);
        if (endptr - buffer + 2 > pos)
            break;

        // store "the rest of line" as function name to function table
        m_functionTable.push_back({ laddr, endptr+2, NO_CLASS });
        cnt++;

        // This logging call usually fills console with loads of messages; commented out for sanity reasons
        //LogFunc(LOG_VERBOSE, "Address: %llu, function: %s", laddr, m_functionTable[laddr].c_str());
    }

    close(readfd);

    // sort function entries to allow effective search
    std::sort(m_functionTable.begin(), m_functionTable.end(), FunctionEntrySortPredicate());

    LogFunc(LOG_VERBOSE, "Loaded %i symbols from supplied binary file", cnt);
}

FunctionEntry* GmonFile::GetFunctionByAddress(uint64_t address, uint32_t* functionIndex)
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
        if (m_functionTable[imid].address > address)
            ihigh = imid - 1;
        else
            ilow = imid + 1;

        imid = (ilow + ihigh) / 2;
    }

    // the outcome may be one step higher (depending from which side we arrived), than we would like to have - we are looking for
    // "highest lower address", i.e. for addresses 2, 5, 10, and input address 7, we return entry with address 5

    if (m_functionTable[ilow].address <= address)
    {
        if (functionIndex)
            *functionIndex = ilow;
        return &m_functionTable[ilow];
    }

    if (functionIndex)
        *functionIndex = ilow - 1;

    return &m_functionTable[ilow - 1];
}

void GmonFile::ProcessFlatProfile()
{
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

    histogram* record;
    uint32_t pcoff;
    uint32_t fi;

    // go through all histogram records and sum the time sampled
    // match function, in which it belongs, and then add sampled time there
    for (std::list<histogram*>::iterator itr = m_histograms.begin(); itr != m_histograms.end(); ++itr)
    {
        record = *itr;
        // histogram samples are divided into "buckets" of address range
        // pcoff is then multiplier of bucket index, so it results into PC offset
        pcoff = 1 + ((record->highpc - record->lowpc) / record->num_bins);

        // go through all buckets (bins)
        for (int i = 0; i < record->num_bins; i++)
        {
            // if there was some time spent..
            if (record->sample[i] > 0)
            {
                // retrieve function entry and index, and add this time to total time spent here
                FunctionEntry *fe = GetFunctionByAddress(record->lowpc + i*pcoff, &fi);
                if (fe)
                    m_flatProfile[fi].timeTotal += record->sample[i];
            }
        }
    }

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

    // at first, sort by call count - that's our secondary criteria
    std::sort(m_flatProfile.begin(), m_flatProfile.end(), FlatProfileCallCountSortPredicate());

    // then, sort by time spent, and use stable sort to not scramble already sorted entires
    // within same "bucket" of time quantum
    std::stable_sort(m_flatProfile.begin(), m_flatProfile.end(), FlatProfileTimeSortPredicate());
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

    LogFunc(LOG_VERBOSE, "Read call graph block, frompc %llu, selfpc %llu, count %lu", cg->frompc, cg->selfpc, cg->count);

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
