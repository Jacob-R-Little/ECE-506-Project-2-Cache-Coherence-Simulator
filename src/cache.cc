/*******************************************************
                          cache.cc
********************************************************/

#include <stdlib.h>
#include <assert.h>
#include "cache.h"
using namespace std;

// #define MESI_FILTER_16_Direct
// #define MESI_FILTER_16_Fully
// #define MESI_FILTER_4_Set_4_Way


Cache::Cache(int s, int a, int b, int p) {
   ulong i, j;
   reads = readMisses = writes = 0;
   writeMisses = writeBacks = currentCycle = 0;
   cache_to_cache_transfers = mem_transactions = interventions = invalidations = flushes = numBusRdX = numBusUpgr = 0;
   useful_snoops = wasted_snoops = filtered_snoops = 0;

   size = (ulong)(s);
   lineSize = (ulong)(b);
   assoc = (ulong)(a);
   sets = (ulong)((s / b) / a);
   numLines = (ulong)(s / b);
   log2Sets = (ulong)(log2(sets));
   log2Blk = (ulong)(log2(b));
   protocol = p;

   tagMask = 0;
   for (i = 0;i < log2Sets;i++) {
      tagMask <<= 1;
      tagMask |= 1;
   }

   /**create a two dimentional cache, sized as cache[sets][assoc]**/
   cache = new cacheLine * [sets];
   for (i = 0; i < sets; i++) {
      cache[i] = new cacheLine[assoc];
      for (j = 0; j < assoc; j++) {
         cache[i][j].invalidate();
      }
   }

   // 

#if defined MESI_FILTER_16_Fully
   if (protocol == MESI_Filter) {
      filter = (Cache *)malloc(sizeof(Cache));
      filter = new Cache(16 * b, 16, b, MSI);
   }
#elif defined MESI_FILTER_4_Set_4_Way
   if (protocol == MESI_Filter) {
      filter = (Cache *)malloc(sizeof(Cache));
      filter = new Cache(4 * b, 4, b, MSI);
   }
#else 
   if (protocol == MESI_Filter) {
      filter = (Cache *)malloc(sizeof(Cache));
      filter = new Cache(16 * b, 1, b, MSI);
   }
#endif

}

/**you might add other parameters to Access()
since this function is an entry point
to the memory hierarchy (i.e. caches)**/
uint Cache::Access(ulong addr, uchar op) {
   uint prev_state;

   currentCycle++;/*per cache global counter to maintain LRU order
                    among cache ways, updated on every cache access*/

   if (op == 'w') writes++;
   else          reads++;

   if (protocol == MESI_Filter) filter->clearLine(addr);

   cacheLine *line = findLine(addr);

   if (line == NULL) { // miss
      cacheLine *newline = fillLine(addr);

      if (op == 'w') {
         writeMisses++;
         newline->setFlags(MODIFIED);
         return sendBusTrans(BusRdX);
      } else {
         readMisses++;
         newline->setFlags(SHARED);
         mem_transactions++;
         return sendBusTrans(BusRd);
      }
   } else { // hit
      /**since it's a hit, update LRU and update dirty flag**/
      updateLRU(line);
      prev_state = line->getFlags();

      switch (protocol) {
         case MSI:
            if (op == 'w') {
               line->setFlags(MODIFIED);
               if (prev_state != MODIFIED) return sendBusTrans(BusRdX);
            } else {
               if (prev_state == INVALID) {
                  line->setFlags(SHARED);
                  mem_transactions++;
                  return sendBusTrans(BusRd);
               }
            }
            break;
         case MSI_BusUpgr:
            if (op == 'w') {
               line->setFlags(MODIFIED);
               if (prev_state == SHARED) return sendBusTrans(BusUpgr);
               if (prev_state == INVALID) return sendBusTrans(BusRdX);
            } else {
               if (prev_state == INVALID) {
                  line->setFlags(SHARED);
                  mem_transactions++;
                  return sendBusTrans(BusRd);
               }
            }
            break;
         case MESI:
         case MESI_Filter:
            if (op == 'w') {
               line->setFlags(MODIFIED);
               if (prev_state == SHARED) return sendBusTrans(BusUpgr);
               if (prev_state == INVALID) {
                  // mem_transactions--;
                  return sendBusTrans(BusRdX);
               }
            } else {
               if (prev_state == INVALID) {
                  line->setFlags(SHARED);
                  mem_transactions++;
                  return sendBusTrans(BusRd);
               }
            }
            break;
         default:
            break;
      }
   }

   return BusNone;
}

bool Cache::Snoop(ulong addr, uint trans) {

   cacheLine *line;

   if (protocol == MESI_Filter) {
      if (filter->findLine(addr)) {
         filtered_snoops++;
         return false;
      }
   }

   line = findLine(addr);

   if (line == NULL) {
      wasted_snoops++;
      if (protocol == MESI_Filter) filter->fillLine(addr);
      return false;
   }

   switch (protocol) {
      case MSI:
         Snoop_MSI(trans, line);
         break;
      case MSI_BusUpgr:
         Snoop_MSI_BusUpgr(trans, line);
         break;
      case MESI:
         Snoop_MESI(trans, line);
         break;
      case MESI_Filter:
         Snoop_MESI_Filter(addr, trans, line);
         break;
      default:
         break;
   }

   useful_snoops++;
   return true;
}

void Cache::Snoop_MSI(uint trans, cacheLine *line) {
   switch (line->getFlags()) {
      case MODIFIED:
         switch (trans) {
            case BusRd:
               line->setFlags(SHARED);
               flushes++;
               interventions++;
               writeBack();
               break;
            case BusRdX:
               line->setFlags(INVALID);
               invalidations++;
               flushes++;
               writeBack();
               break;
         }
         break;
      case SHARED:
         switch (trans) {
            case BusRd:
               break;
            case BusRdX:
               line->setFlags(INVALID);
               invalidations++;
               break;
         }
         break;
      case INVALID:
      default:
         break;
   }
}

void Cache::Snoop_MSI_BusUpgr(uint trans, cacheLine *line) {
   switch (line->getFlags()) {
      case MODIFIED:
         switch (trans) {
            case BusRd:
               line->setFlags(SHARED);
               flushes++;
               interventions++;
               writeBack();
               break;
            case BusRdX:
               line->setFlags(INVALID);
               invalidations++;
               flushes++;
               writeBack();
               break;
            case BusUpgr:
               break;
         }
         break;
      case SHARED:
         switch (trans) {
            case BusRd:
               break;
            case BusRdX:
               line->setFlags(INVALID);
               invalidations++;
               break;
            case BusUpgr:
               line->setFlags(INVALID);
               invalidations++;
               break;
         }
         break;
      case INVALID:
      default:
         break;
   }
}

void Cache::Snoop_MESI(uint trans, cacheLine *line) {
   switch (line->getFlags()) {
      case MODIFIED:
         switch (trans) {
            case BusRd:
               line->setFlags(SHARED);
               interventions++;
               flushes++;
               writeBack();
               break;
            case BusRdX:
               line->setFlags(INVALID);
               invalidations++;
               flushes++;
               writeBack();
               break;
            case BusUpgr:
               break;
         }
         break;
      case EXCLUSIVE:
         switch (trans) {
            case BusRd:
               line->setFlags(SHARED);
               interventions++;
               break;
            case BusRdX:
               line->setFlags(INVALID);
               invalidations++;
               break;
            case BusUpgr:
               break;
         }
         break;
      case SHARED:
         switch (trans) {
            case BusRd:
               break;
            case BusRdX:
               line->setFlags(INVALID);
               invalidations++;
               break;
            case BusUpgr:
               line->setFlags(INVALID);
               invalidations++;
               break;
         }
         break;
      case INVALID:
      default:
         break;
   }
}

void Cache::Snoop_MESI_Filter(ulong addr, uint trans, cacheLine *line) {
   switch (line->getFlags()) {
      case MODIFIED:
         switch (trans) {
            case BusRd:
               line->setFlags(SHARED);
               interventions++;
               flushes++;
               writeBack();
               break;
            case BusRdX:
               line->setFlags(INVALID);
               filter->fillLine(addr);
               invalidations++;
               flushes++;
               writeBack();
               break;
            case BusUpgr:
               break;
         }
         break;
      case EXCLUSIVE:
         switch (trans) {
            case BusRd:
               line->setFlags(SHARED);
               interventions++;
               break;
            case BusRdX:
               line->setFlags(INVALID);
               filter->fillLine(addr);
               invalidations++;
               break;
            case BusUpgr:
               break;
         }
         break;
      case SHARED:
         switch (trans) {
            case BusRd:
               break;
            case BusRdX:
               line->setFlags(INVALID);
               filter->fillLine(addr);
               invalidations++;
               break;
            case BusUpgr:
               line->setFlags(INVALID);
               filter->fillLine(addr);
               invalidations++;
               break;
         }
         break;
      case INVALID:
      default:
         break;
   }
}

/*look up line*/
cacheLine *Cache::findLine(ulong addr) {
   ulong i, j, tag, pos;

   pos = assoc;
   tag = calcTag(addr);
   i = calcIndex(addr);

   for (j = 0; j < assoc; j++)
      if (cache[i][j].isValid()) {
         if (cache[i][j].getTag() == tag) {
            pos = j;
            break;
         }
      }
   if (pos == assoc) {
      return NULL;
   } else {
      return &(cache[i][pos]);
   }
}

/*upgrade LRU line to be MRU line*/
void Cache::updateLRU(cacheLine *line) {
   line->setSeq(currentCycle);
}

/*return an invalid line as LRU, if any, otherwise return LRU line*/
cacheLine *Cache::getLRU(ulong addr) {
   ulong i, j, victim, min;

   victim = assoc;
   min = currentCycle;
   i = calcIndex(addr);

   for (j = 0;j < assoc;j++) {
      if (cache[i][j].isValid() == 0) {
         return &(cache[i][j]);
      }
   }

   for (j = 0;j < assoc;j++) {
      if (cache[i][j].getSeq() <= min) {
         victim = j;
         min = cache[i][j].getSeq();
      }
   }

   assert(victim != assoc);

   return &(cache[i][victim]);
}

/*find a victim, move it to MRU position*/
cacheLine *Cache::findLineToReplace(ulong addr) {
   cacheLine *victim = getLRU(addr);
   updateLRU(victim);

   return (victim);
}

/*allocate a new line*/
cacheLine *Cache::fillLine(ulong addr) {
   ulong tag;

   cacheLine *victim = findLineToReplace(addr);
   assert(victim != 0);

   if (victim->getFlags() == MODIFIED) {
      writeBack();
   }

   tag = calcTag(addr);
   victim->setTag(tag);

   if (true) victim->setFlags(SHARED);
   else victim->setFlags(EXCLUSIVE);

   /**note that this cache line has been already
      upgraded to MRU in the previous function (findLineToReplace)**/

   return victim;
}

void Cache::clearLine(ulong addr) {
   cacheLine *victim = findLine(addr);
   if (victim == NULL) return;
   victim->setTag(0);
   victim->setFlags(INVALID);
}

uint Cache::sendBusTrans(uint trans) {
   switch (trans) {
      case BusRdX:
         mem_transactions++;
         numBusRdX++;
         break;
      case BusUpgr:
         numBusUpgr++;
         break;
      default:
         break;
   }
   return trans;
}

void Cache::fixAccess(ulong addr, uint trans, bool C) {
   cacheLine *line = findLine(addr);
   if (trans == BusRd) {
      if (C) {
         line->setFlags(SHARED);
         cache_to_cache_transfers++;
         mem_transactions--;
      } else {
         line->setFlags(EXCLUSIVE);
      }
   }
   if (trans == BusRdX && C) {
      line->setFlags(MODIFIED);
      cache_to_cache_transfers++;
      mem_transactions--;
   }



}

void Cache::printStats(uint proc) {
   printf("============ Simulation results (Cache %d) ============\n", proc);
   printf("01. number of reads: %d\n", (int)reads);
   printf("02. number of read misses: %d\n", (int)readMisses);
   printf("03. number of writes: %d\n", (int)writes);
   printf("04. number of write misses: %d\n", (int)writeMisses);
   printf("05. total miss rate: %.2f%%\n", 100 * ((double)(readMisses + writeMisses)) / ((double)(reads + writes)));
   printf("06. number of writebacks: %d\n", (int)writeBacks);
   printf("07. number of cache-to-cache transfers: %d\n", (int)cache_to_cache_transfers);
   printf("08. number of memory transactions: %d\n", (int)mem_transactions);
   printf("09. number of interventions: %d\n", (int)interventions);
   printf("10. number of invalidations: %d\n", (int)invalidations);
   printf("11. number of flushes: %d\n", (int)flushes);
   printf("12. number of BusRdX: %d\n", (int)numBusRdX);
   printf("13. number of BusUpgr: %d\n", (int)numBusUpgr);
   if (protocol == MESI_Filter) {
      printf("14. number of useful snoops: %d\n", (int)useful_snoops);
      printf("15. number of wasted snoops: %d\n", (int)wasted_snoops);
      printf("16. number of filtered snoops: %d\n", (int)filtered_snoops);
   }
}
