/*===========================================================================*
 * This file is part of a solver for the Vehicle Routing Problem             *
 * developed using the BiCePS Linear Integer Solver (BLIS).                  *
 *                                                                           *
 * This solver is distributed under the Common Public License as part of     * 
 * the COIN-OR repository (http://www.coin-or.org).                          *
 *                                                                           *
 * Authors: Yan Xu, Lehigh University                                        *
 *          Ted Ralphs, Lehigh University                                    *
 *                                                                           *
 * Copyright (C) 2007 Yan Xu and Ted Ralphs.                                 *
 * All Rights Reserved.                                                      *
 *===========================================================================*/

#ifndef VrpModel_h_
#define VrpModel_h_

//#############################################################################

#include <vector>

#include "BlisModel.h"
#include "VrpVariable.h"
#include "VrpCommonTypes.h"
#include "VrpConstants.h"
#include "VrpParams.h"
#include "VrpCutGenerator.h"

//#############################################################################

/** Model class for VRP. */
class VrpModel : public BlisModel 
{    

   friend class VrpCutGenerator;
   
 private:

   char name_[100];
   int vertnum_;
   int edgenum_;
   int numroutes_;
   int depot_;
   int capacity_;
   int wtype_;
   int *demand_;    /*vertnum_*/
   int *posx_;      /*vertnum_*/
   int *posy_;      /*vertnum_*/
   double *coordx_; /*vertnum_*/
   double *coordy_; /*vertnum_*/
   double *coordz_; /*vertnum_*/
   double etol_;

   VrpParams *VrpPar_;
   VrpNetwork *n_;  /* Allocate when readInstance */
   
   // edges_ hold the same elements as variables_ does, do not free memory.
   // For parallel, reinsert elements in variables_ to edges_
   std::vector<VrpVariable *> edges_;

   // Do we need this? Solution?
   std::vector<best_tours> curTour_;

protected:

   /** 1) Set colMatrix_, varLB_, varUB_, conLB_, conUB, numCols_, numRows_
    *  2) Set objCoef_ and objSense_
    *  3) Set colType_ ('C', 'I', or 'B')
    */
   void setModelData();
   
public:

    /** Default construtor. */
   VrpModel() : vertnum_(0), edgenum_(0), numroutes_(0), depot_(0),
      capacity_(0), wtype_(0), etol_(1e-5){
      demand_ = 0;
      posx_ = 0;
      posy_ = 0;
      coordx_ = 0;
      coordy_ = 0;
      coordz_ = 0;
      n_ = 0;
      VrpPar_ = new VrpParams;

      AlpsPar()->setEntry(AlpsParams::searchStrategy, ALPS_SEARCH_BEST);

      BlisPar()->setEntry(BlisParams::cutClique,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::cutFlowCover,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::cutGomory,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::cutKnapsack,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::cutMir,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::cutOddHole,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::cutProbing,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::cutTwoMir,BLIS_NONE);
      BlisPar()->setEntry(BlisParams::heurRound,BLIS_NONE);

      // Cuts as formulation
      BlisPar()->setEntry(BlisParams::cutFactor, ALPS_DBL_MAX);
      BlisPar()->setEntry(BlisParams::cutPass, ALPS_INT_MAX);
      BlisPar()->setEntry(BlisParams::tailOff, -1000.0);
      BlisPar()->setEntry(BlisParams::denseConFactor, ALPS_DBL_MAX);
   }
   
   VrpModel(const char* dataFile) : etol_(1e-5){
      VrpPar_ = new VrpParams;
      readInstance(dataFile);
      VrpCutGenerator *cg = new VrpCutGenerator(this, vertnum_);
      cg->setStrategy(1);
      addCutGenerator(cg);
   }
   
   /** Destructor. */
   virtual ~VrpModel() {
      delete [] demand_; demand_ = 0;
      delete [] posx_; posx_ = 0;
      delete [] posy_; posy_ = 0;
      delete [] coordx_; coordx_ = 0;
      delete [] coordy_; coordy_ = 0;
      delete [] coordz_; coordz_ = 0;
      delete    VrpPar_; VrpPar_ = 0;
      delete    n_; n_ = 0;
   }
   
   /** For parallel code, only the master calls this function.
    *  1) Read in the instance data
    *  2) Set colMatrix_, varLB_, varUB_, conLB_, conUB
    *     numCols_, numRows_
    *  3) Set objCoef_ and objSense_
    *  4) Set colType_ ('C', 'I', or 'B')
    *  5) Create variables and constraints
    *  6) Set numCoreVariables_ and numCoreConstraints_
    */
   virtual void readInstance(const char* dateFile);
   
   /** Read in Alps, Blis, Vrp parameters. */
   virtual void readParameters(const int argnum, const char * const *arglist);
   
   /** User's criteria for a feasible solution. Return true (feasible)
    *	or false (infeasible) 
    */
   virtual bool userFeasibleSolution();
   
   int index (int v0, int v1) {
      return(v0 < v1 ? v1*(v1 - 1)/2 + v0 : v0*(v0 - 1)/2 + v1);
   }
   
   int computeCost(int v0, int v1); 
   
   std::vector<VrpVariable *> getEdgeList() { return edges_; }

   CoinPackedVector *getSolution();

   void createNet(CoinPackedVector *vec);

    /** Register knowledge. */
    virtual void registerKnowledge();  

    /** Pack Vrp portion of the model into an encoded object. */
    AlpsReturnCode encodeVrp(AlpsEncoded *encoded) const;

    /** Unpack Vrp portion of the model from an encoded object. */
    AlpsReturnCode decodeVrp(AlpsEncoded &encoded);  
    
    /** The method that encodes the model into an encoded object. */
    virtual AlpsEncoded* encode() const;
    
    /** The method that decodes the model from an encoded object. */
    virtual void decodeToSelf(AlpsEncoded&);
   
};

//#############################################################################

#endif
