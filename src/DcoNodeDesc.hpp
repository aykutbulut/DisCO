/*===========================================================================*
 * This file is part of the BiCePS Linear Integer Solver (BLIS).             *
 *                                                                           *
 * BLIS is distributed under the Eclipse Public License as part of the       *
 * COIN-OR repository (http://www.coin-or.org).                              *
 *                                                                           *
 * Authors:                                                                  *
 *                                                                           *
 *          Yan Xu, Lehigh University                                        *
 *          Ted Ralphs, Lehigh University                                    *
 *                                                                           *
 * Conceptual Design:                                                        *
 *                                                                           *
 *          Yan Xu, Lehigh University                                        *
 *          Ted Ralphs, Lehigh University                                    *
 *          Laszlo Ladanyi, IBM T.J. Watson Research Center                  *
 *          Matthew Saltzman, Clemson University                             *
 *                                                                           * 
 *                                                                           *
 * Copyright (C) 2001-2015, Lehigh University, Yan Xu, and Ted Ralphs.       *
 * All Rights Reserved.                                                      *
 *===========================================================================*/

#ifndef DcoNodeDesc_h_
#define DcoNodeDesc_h_

//#############################################################################

#include "CoinWarmStartBasis.hpp"

#include "AlpsNodeDesc.h"
#include "BcpsNodeDesc.h"

#include "DcoHelp.hpp"
#include "DcoModel.hpp"

//#############################################################################


class DcoNodeDesc : public BcpsNodeDesc {

 private:
    
    /** Branched direction to create it. For updating pseudocost. */
    int branchedDir_;

    /** Branched object index to create it. For updating pseudocost. */
    int branchedInd_;

    /** Branched value to create it. For updating pseudocost. */
    double branchedVal_;

    /** Warm start. */
    CoinWarmStartBasis *basis_;
    
 public:

    /** Default constructor. */
    DcoNodeDesc() :
        BcpsNodeDesc(),
        branchedDir_(0),
        branchedInd_(-1),
        branchedVal_(0.0),
	basis_(NULL)
        {}

    /** Useful constructor. */
    DcoNodeDesc(DcoModel* m) 
	:
	BcpsNodeDesc(m),
        branchedDir_(0),
        branchedInd_(-1),
        branchedVal_(0.0),
	basis_(NULL)
	{}

    /** Destructor. */
        virtual ~DcoNodeDesc() { delete basis_; basis_ = NULL;  }

    /** Set basis. */ 
    void setBasis(CoinWarmStartBasis *&ws) { 
        if (basis_) { delete basis_; }
        basis_= ws;
        ws = NULL; 
    }

    /** Get warm start basis. */
    CoinWarmStartBasis * getBasis() const { return basis_; }

    /** Set branching direction. */
    void setBranchedDir(int d) { branchedDir_ = d; }

    /** Get branching direction. */
    int getBranchedDir() const { return branchedDir_; }

    /** Set branching object index. */
    void setBranchedInd(int d) { branchedInd_ = d; }

    /** Get branching object index. */
    int getBranchedInd() const { return branchedInd_; }

    /** Set branching value. */
    void setBranchedVal(double d) { branchedVal_ = d; }

    /** Get branching direction. */
    double getBranchedVal() const { return branchedVal_; }

 protected:

    /** Pack Disco portion of node description into an encoded. */
    AlpsReturnStatus encodeDco(AlpsEncoded *encoded) const {
	AlpsReturnStatus status = AlpsReturnStatusOk;

	encoded->writeRep(branchedDir_);
	encoded->writeRep(branchedInd_);
	encoded->writeRep(branchedVal_);

	// Basis
	int ava = 0;
	if (basis_) {
	    ava = 1;
	    encoded->writeRep(ava);
	    DcoEncodeWarmStart(encoded, basis_);
	}
	else {
	    encoded->writeRep(ava);
	}
	
	return status;
    }

    /** Unpack Disco portion of node description from an encoded. */
    AlpsReturnStatus decodeDco(AlpsEncoded &encoded) {
	AlpsReturnStatus status = AlpsReturnStatusOk;
	
	encoded.readRep(branchedDir_);
	encoded.readRep(branchedInd_);
	encoded.readRep(branchedVal_);
	
	// Basis
	int ava;
	encoded.readRep(ava);
	if (ava == 1) {
            if (basis_) delete basis_;
	    basis_ = DcoDecodeWarmStart(encoded, &status);
	}
	else {
	    basis_ = NULL;
	}
	
	return status;
    }

 public:

    /** Pack node description into an encoded. */
    virtual AlpsReturnStatus encode(AlpsEncoded *encoded) const {
    	AlpsReturnStatus status = AlpsReturnStatusOk;
	
	status = encodeBcps(encoded);
	status = encodeDco(encoded);
	
	return status;
    }

    /** Unpack a node description from an encoded. Fill member data. */
    virtual AlpsReturnStatus decode(AlpsEncoded &encoded) {
	
    	AlpsReturnStatus status = AlpsReturnStatusOk;
	
	status = decodeBcps(encoded);
	status = decodeDco(encoded);

	return status;
    }
    
};
#endif