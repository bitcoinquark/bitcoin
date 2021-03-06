// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <crypto/equihash.h>
#include <primitives/block.h>
#include <streams.h>
#include <uint256.h>
#include <util.h>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
	unsigned int nProofOfWorkLimitLegacy = UintToArith256(params.powLimitLegacy).GetCompact();

	// Genesis block
	if (pindexLast == NULL)
	   return nProofOfWorkLimitLegacy;

	int nHeight = pindexLast->nHeight + 1;
	bool postfork = nHeight >= params.BTQHeight;
	unsigned int nProofOfWorkLimit = UintToArith256(params.PowLimit(postfork)).GetCompact();

	if (nHeight < params.BTQHeight) {
	   return BitcoinGetNextWorkRequired(pindexLast, pblock, params);
	}
	else if (nHeight < params.BTQHeight + params.BTQPremineWindow) {
	   return nProofOfWorkLimit;
	}
	else if(nHeight < params.BTQHeight + params.BTQPremineWindow + params.nPowAveragingWindow) {
	   return UintToArith256(params.powLimitStart).GetCompact();
	}

	// Difficulty adjustement mechanism in case of abrupt hashrate loss
	if(postfork) {

		uint32_t nBits = pindexLast->nBits;
		if (nBits != nProofOfWorkLimit) {
		    const CBlockIndex *pindex6 = pindexLast->GetAncestor(nHeight - 7);
		    assert(pindex6);
		    int64_t mtp6blocks = pindexLast->GetMedianTimePast() - pindex6->GetMedianTimePast();

		    if (mtp6blocks > 12 * 3600) {

			    // If producing the last 6 block took more than 12h, increase the difficulty
			    // target by 1/4 (which reduces the difficulty by 20%). This ensure the
			    // chain do not get stuck in case we lose hashrate abruptly.
			    arith_uint256 nPow;
			    nPow.SetCompact(nBits);
			    nPow += (nPow >> 2);

			    // Make sure we do not go bellow allowed values.
			    const arith_uint256 bnPowLimit = UintToArith256(params.PowLimit(postfork));
			    if (nPow > bnPowLimit) nPow = bnPowLimit;

			    return nPow.GetCompact();
		    }
		}
	}

	// Simple moving average over work difficulty adjustement algorithm.
	const CBlockIndex* pindexFirst = pindexLast;
	arith_uint256 bnTot {0};
	for (int i = 0; pindexFirst && i < params.nPowAveragingWindow; i++) {
	   arith_uint256 bnTmp;
	   bnTmp.SetCompact(pindexFirst->nBits);
	   bnTot += bnTmp;
	   pindexFirst = pindexFirst->pprev;
	}

	if (pindexFirst == NULL)
	   return nProofOfWorkLimit;

	arith_uint256 bnAvg {bnTot / params.nPowAveragingWindow};

	return CalculateNextWorkRequired(bnAvg, pindexLast->GetMedianTimePast(), pindexFirst->GetMedianTimePast(), params);
}

unsigned int CalculateNextWorkRequired(arith_uint256 bnAvg, int64_t nLastBlockTime, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    // Limit adjustment
    int64_t nActualTimespan = nLastBlockTime - nFirstBlockTime;
    nActualTimespan = params.AveragingWindowTimespan() + (nActualTimespan - params.AveragingWindowTimespan())/4;

    if (nActualTimespan < params.MinActualTimespan())
        nActualTimespan = params.MinActualTimespan();
    if (nActualTimespan > params.MaxActualTimespan())
        nActualTimespan = params.MaxActualTimespan();

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.PowLimit(true));
    arith_uint256 bnNew {bnAvg};
    bnNew /= params.AveragingWindowTimespan();
    bnNew *= nActualTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckEquihashSolution(const CBlockHeader *pblock, const CChainParams& params)
{
    unsigned int n = params.EquihashN();
    unsigned int k = params.EquihashK();

    // Hash state
    crypto_generichash_blake2b_state state;
    EhInitialiseState(n, k, state);

    // I = the block header minus nonce and solution.
    CEquihashInput I{*pblock};
    // I||V
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << I;
    ss << pblock->nNonce;

    // H(I||V||...
    crypto_generichash_blake2b_update(&state, (unsigned char*)&ss[0], ss.size());

    bool isValid;
    EhIsValidSolution(n, k, state, pblock->nSolution, isValid);
    if (!isValid)
        return error("CheckEquihashSolution(): invalid solution");

    return true;
}

unsigned int BitcoinGetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    unsigned int nProofOfWorkLimit = UintToArith256(params.PowLimit(false)).GetCompact();

    int nHeightNext = pindexLast->nHeight + 1;

    if (nHeightNext % params.DifficultyAdjustmentInterval() != 0)
    {
        // Difficulty adjustment interval is not finished. Keep the last value.
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then allow mining of a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return BitcoinCalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int BitcoinCalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespanLegacy/4)
        nActualTimespan = params.nPowTargetTimespanLegacy/4;
    if (nActualTimespan > params.nPowTargetTimespanLegacy*4)
        nActualTimespan = params.nPowTargetTimespanLegacy*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.PowLimit(false));
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespanLegacy;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, bool postfork, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.PowLimit(postfork)))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
