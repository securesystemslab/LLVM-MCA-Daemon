
//===----------------------- BranchUnit.cpp -----------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Work in progress
///
//===----------------------------------------------------------------------===//


#define DEBUG_TYPE "llvm-mca"

#include <optional>
#include "MetadataCategories.h"
#include "AbstractBranchPredictorUnit.h"
#include "SkylakeBranchUnit.h"
#include <climits>
#include <cstdint>
#include <numeric>

#include <iostream>

namespace llvm {
namespace mcad {
	

	SkylakeBranchUnit::SkylakeBranchUnit(uint32_t penalty_) {
		penalty = penalty_;
	}
	
	uint32_t SkylakeBranchUnit::getMispredictionPenalty() {
		return penalty;
	}

    void SkylakeBranchUnit::printSingleTable(SkylakeBranchTable& pht) {
        for (auto i = pht.begin(); i != pht.end(); i++) {
            std::cout << "Index " << i->first.to_ullong() << ": ";
            for (auto j = i->second.begin(); j != i->second.end(); j++)
                std::cout << "(" << j->pc << ", " << j->useful << "), ";
            std::cout << std::endl;
        }

    }

    void SkylakeBranchUnit::printTable() {
    
/*
        std::cout << "PHT 1\n";
        printSingleTable(pht1);
        std::cout << "PHT 2\n";
        printSingleTable(pht2);
        std::cout << "PHT 3\n";
        printSingleTable(pht3);

        std::cout << "Base\n";
        printSingleTable(base);
        */
    }

    AbstractBranchPredictorUnit::BranchDirection SkylakeBranchUnit::predictBranch(MDInstrAddr pc) {
		return predictBranch(pc, MDInstrAddr{0});
	}

    AbstractBranchPredictorUnit::BranchDirection SkylakeBranchUnit::predictBranch(MDInstrAddr pc, MDInstrAddr target) {
		SkylakeBranchEntry* entry = nullptr;
        // See if present in any table
        // Greedily accepts first table where present
        auto test = updatePHR(pc, target);
		entry = getTable(pht1, getPHTIndex(test, 0),      pc, entry);
		entry = getTable(pht2, getPHTIndex(test, 3),     pc, entry);
		entry = getTable(pht3, getPHTIndex(test, 11),     pc, entry);
        entry = getTable(base, SkylakePHR(pc.addr & 0x1FFF), pc, entry); // Check base last
		
        if (entry != nullptr) {
            // Branch taken
            entry->useful++;
            return BranchDirection::TAKEN;
        }
        return BranchDirection::NOT_TAKEN;
    }
	
    SkylakeBranchUnit::SkylakeBranchEntry* SkylakeBranchUnit::getTable(SkylakeBranchTable& pht, 
                                                    SkylakePHR index, 
                                                    MDInstrAddr pc, 
                                                    SkylakeBranchEntry* out) { 
		if (out != nullptr)
			return out;
		
		auto exists = pht.find(index);
		if (exists != pht.end())
			for (int i = 0; i < exists->second.size(); i++)
				if (exists->second[i].pc == pc.addr) 
                    return &exists->second[i];
                
		return out;
	}

	// Functions currently implementing Skylake behavior
	// Can make class virtual for architecture compatibility in the future
	void SkylakeBranchUnit::insertTable(SkylakeBranchTable& pht, MDInstrAddr pc, SkylakePHR index) {

		auto exists = pht.find(index);
		if (exists != pht.end())
			// If index exists, put into that table
			phtSetPush(pht, pc, index);
		else if (pht.size() < 2048) {
			// Does not exist, but room to add table
			pht[index] = {};
			phtSetPush(pht, pc, index);
		}
		else {
			// Need to evict a table
			// Evict row where total prediction score is minimum
			SkylakeBranchTable::iterator to_remove;
			uint32_t check = UINT_MAX;
			for (auto e = pht.begin(); e != pht.end(); e++) {
				uint32_t current = 0;
                for (auto i = e->second.begin(); i != e->second.end(); i++)
                    current += i->useful;
				to_remove = (check < current) ? to_remove : e;
				check = check < current ? check : current;
			}
			pht.erase(to_remove);
			pht[index] = {};
			phtSetPush(pht, pc, index);
		}
	}
	void SkylakeBranchUnit::phtSetPush(SkylakeBranchTable& pht, MDInstrAddr pc, SkylakePHR index) {
		if (pht[index].size() >= 4) {
			auto to_remove = std::min_element(pht[index].begin(), pht[index].end());
			pht[index].erase(to_remove);
		}
		pht[index].push_back(SkylakeBranchEntry(pc.addr,0));
	}

	void SkylakeBranchUnit::recordTakenBranch(MDInstrAddr pc, BranchDirection nextInstrDirection) {
        if (nextInstrDirection == BranchDirection::TAKEN)
            recordTakenBranch(pc, {0});
    }

	void SkylakeBranchUnit::recordTakenBranch(MDInstrAddr pc, MDInstrAddr target) {
	 // TODO: Get correct index for each table
        
        // Base predictor
        auto base_index = SkylakePHR(pc.addr & 0x1FFF);
        phtSetPush(base, pc, base_index);
        
        // PHTs
	 	// See page 9 of H&H
        phr = updatePHR(pc, target);
		insertTable(pht1, pc, getPHTIndex(phr, 0)); 
		insertTable(pht2, pc, getPHTIndex(phr, 3));
		insertTable(pht3, pc, getPHTIndex(phr, 11));
	}


    // Each table has its own indexing
    // Work in progress
	// Convert PHR to the index for a PHT table
    SkylakeBranchUnit::SkylakePHR SkylakeBranchUnit::getPHTIndex(SkylakePHR phr, int hist_length) { //int start1, int start2) {
        SkylakePHR base;
        // Make mask of 1010...
        /*
        for (int _ = 0; _ < 38; _++) {
            base |= 5;
            base = base << 4;
        }

		
		// Get range of bits from 16(i)-6 to 16(i)+8
		auto index = base << (PHR_LENGTH - (start1+14));
        index = index >> (PHR_LENGTH-14-start1);
        index &= phr;
		
		auto index2 = base << (PHR_LENGTH - (start2+14));
        index2 = index2 >> (PHR_LENGTH-14-start2);
        index2 &= phr;

		// xor two indices together to get final index
		return index ^ index2;
        */
        if (hist_length == 0) {
            SkylakePHR index1, index2;
            int start = 6;
            int end = 20;
            for (int index = start; index <= end; index += 2) {
                index1[0] = phr[index];
                index1 = index1 << 1;
            }
            start = 1;
            end = 15;
            for (int index = start; index <= end; index += 2) {
                index2[0] = phr[index];
                index2 = index2 << 1;
            }

        }
        SkylakePHR result;
        for (int i = 1; i <= hist_length; i++) {
            SkylakePHR index1, index2;
            int start = (16*i) - 6;
            int end = (16*i) + 8;
            for (int index = start; index <= end; index += 2) {
                index1[0] = phr[index];
                index1 = index1 << 1;
            }
            start = (16*i) - 13;
            end = (16*i) + 1;
            for (int index = start; index <= end; index += 2) {
                index2[0] = phr[index];
                index2 = index2 << 1;
            }
            result = result ^ index1 ^ index2;
        }
        return result;

	}

	// Part of PHR
	unsigned long long SkylakeBranchUnit::getFootprint(MDInstrAddr branchInstr, MDInstrAddr targetInstr) {
		// branchAddr = (branchAddr >> 3) & 0x3FFFF;
		uint32_t branchAddr = branchInstr.addr, targetAddr = targetInstr.addr;

		targetAddr = targetAddr & 0x001F;

		uint32_t result = 0;
		uint32_t branchRight = ((branchAddr & 0x18) >> 3) 
								| ((branchAddr & 0x180) >> 5) 
								| ((branchAddr & 0x1800) >> 7);

		uint32_t branchLeft = ((branchAddr & 0x60) >> 5) 
								| ((branchAddr & 0x600) >> 7)
								| ((branchAddr & 0x7E000) >> 9);

		result |= branchRight ^ targetAddr;
		result |= branchLeft << 6;

		return result;
	}

    SkylakeBranchUnit::SkylakePHR SkylakeBranchUnit::updatePHR(MDInstrAddr currentAddr, MDInstrAddr targetAddr) {
        auto next = phr << 2;
		return next ^ SkylakePHR(getFootprint(currentAddr, targetAddr));
	}


 } // namespace mca
} // namespace llvm

