/*******************************************************
                          cache.h
********************************************************/

#ifndef CACHE_H
#define CACHE_H

#include <cmath>
#include <iostream>

typedef unsigned long ulong;
typedef unsigned char uchar;
typedef unsigned int uint;

/****add new states, based on the protocol****/
enum {
   INVALID = 0,
   SHARED,
   EXCLUSIVE,
   MODIFIED
};

// protocal
enum {
   MSI = 0,
   MSI_BusUpgr,
   MESI,
   MESI_Filter
};

// bus signals
enum {
   BusNone = 0,
   BusRd,
   BusRdX,
   BusUpgr
};

class cacheLine
{
protected:
   ulong tag;
   ulong Flags;
   ulong seq;

public:
   cacheLine() { tag = 0; Flags = INVALID; }
   ulong getTag() { return tag; }
   ulong getFlags() { return Flags; }
   ulong getSeq() { return seq; }
   void setSeq(ulong Seq) { seq = Seq; }
   void setFlags(ulong flags) { Flags = flags; }
   void setTag(ulong a) { tag = a; }
   void invalidate() { tag = 0; Flags = INVALID; } //useful function
   bool isValid() { return ((Flags) != INVALID); }
};

class Cache
{
protected:
   ulong size, lineSize, assoc, sets, log2Sets, log2Blk, tagMask, numLines, protocol;
   ulong reads, readMisses, writes, writeMisses, writeBacks;
   ulong cache_to_cache_transfers, mem_transactions, interventions, invalidations, flushes, numBusRdX, numBusUpgr;
   ulong useful_snoops, wasted_snoops, filtered_snoops;
   Cache *filter;

   cacheLine **cache;
   ulong calcTag(ulong addr) { return (addr >> (log2Blk)); }
   ulong calcIndex(ulong addr) { return ((addr >> log2Blk) & tagMask); }
   ulong calcAddr4Tag(ulong tag) { return (tag << (log2Blk)); }

public:
   ulong currentCycle;

   Cache(int, int, int, int);
   ~Cache() { delete cache; }

   cacheLine *findLineToReplace(ulong addr);
   cacheLine *fillLine(ulong addr);
   void clearLine(ulong addr);
   cacheLine *findLine(ulong addr);
   cacheLine *getLRU(ulong);

   ulong getRM() { return readMisses; }
   ulong getWM() { return writeMisses; }
   ulong getReads() { return reads; }
   ulong getWrites() { return writes; }
   ulong getWB() { return writeBacks; }

   void writeBack() { writeBacks++; mem_transactions++; }
   uint Access(ulong, uchar);
   bool Snoop(ulong, uint);
   void Snoop_MSI(uint, cacheLine *);
   void Snoop_MSI_BusUpgr(uint, cacheLine *);
   void Snoop_MESI(uint, cacheLine *);
   void Snoop_MESI_Filter(ulong, uint, cacheLine *);
   void fixAccess(ulong, uint, bool);
   void printStats(uint);
   void updateLRU(cacheLine *);

   //******///
   //add other functions to handle bus transactions///
   //******///

   uint sendBusTrans(uint);

};

#endif
