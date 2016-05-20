// CoinUtils
#include <CoinMpsIO.hpp>

// Disco headers
#include "DcoModel.hpp"
#include "DcoMessage.hpp"
#include "DcoTreeNode.hpp"
#include "DcoNodeDesc.hpp"
#include "DcoVariable.hpp"
#include "DcoLinearConstraint.hpp"
#include "DcoConicConstraint.hpp"
#include "DcoBranchStrategyMaxInf.hpp"
#include "DcoBranchStrategyPseudo.hpp"
#include "DcoConGenerator.hpp"
#include "DcoLinearConGenerator.hpp"
#include "DcoConicConGenerator.hpp"
#include "DcoSolution.hpp"

// MILP cuts
#include <CglCutGenerator.hpp>
#include <CglProbing.hpp>
#include <CglClique.hpp>
#include <CglOddHole.hpp>
#include <CglFlowCover.hpp>
#include <CglKnapsackCover.hpp>
#include <CglMixedIntegerRounding2.hpp>
#include <CglGomory.hpp>
#include <CglTwomir.hpp>

// Conic cuts
#include <CglConicCutGenerator.hpp>
#include <CglConicIPM.hpp>
#include <CglConicIPMint.hpp>
#include <CglConicOA.hpp>

// STL headers
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>

DcoModel::DcoModel() {
  solver_ = NULL;
  colLB_ = NULL;
  colUB_ = NULL;
  rowLB_ = NULL;
  rowUB_ = NULL;
  numCols_ = 0;
  numRows_ = 0;
  numLinearRows_ = 0;
  numConicRows_ = 0;
  matrix_ = NULL;
  objSense_ = 0.0;
  objCoef_ = NULL;
  numIntegerCols_ = 0;
  integerCols_ = NULL;
  // todo(aykut) following two might create problems.
  upperBound_ = ALPS_OBJ_MAX;

  currRelGap_ = 1e5;
  currAbsGap_ = 1e5;
  activeNode_ = NULL;
  dcoPar_ = new DcoParams();
  numNodes_ = 0;
  numIterations_ = 0;;
  aveIterations_ = 0;
  numRelaxedCols_ = 0;
  relaxedCols_ = NULL;
  numRelaxedRows_ = 0;
  relaxedRows_ = NULL;
  dcoMessageHandler_ = new CoinMessageHandler();
  dcoMessages_ = new DcoMessage();
  // set branch strategy
  branchStrategy_ = NULL;
  rampUpBranchStrategy_ = NULL;

}

DcoModel::~DcoModel() {
  // solver_ is freed in main function.
  if (matrix_) {
    delete matrix_;
  }
  if (colLB_) {
    delete[] colLB_;
  }
  if (colUB_) {
    delete[] colUB_;
  }
  if (rowLB_) {
    delete[] rowLB_;
  }
  if (rowUB_) {
    delete[] rowUB_;
  }
  if (objCoef_) {
    delete[] objCoef_;
  }
  if (branchStrategy_) {
    delete branchStrategy_;
  }
  if (rampUpBranchStrategy_) {
    delete rampUpBranchStrategy_;
  }
  if (activeNode_) {
    delete activeNode_;
  }
  if (dcoPar_) {
    delete dcoPar_;
  }
  if (dcoMessageHandler_) {
    delete dcoMessageHandler_;
  }
  if (dcoMessages_) {
    delete dcoMessages_;
  }
  if (relaxedCols_) {
    delete[] relaxedCols_;
  }
  if (relaxedRows_) {
    delete[] relaxedRows_;
  }
  std::vector<DcoConGenerator*>::iterator it;
  for (it=conGenerators_.begin(); it!=conGenerators_.end(); ++it) {
    delete *it;
  }
  conGenerators_.clear();
}

#if defined(__OA__)
void DcoModel::setSolver(OsiSolverInterface * solver) {
  solver_ = solver;
}
#else
void DcoModel::setSolver(OsiConicSolverInterface * solver) {
  solver_ = solver;
}
#endif

void DcoModel::readInstance(char const * dataFile) {
  // get input file name
  std::string input_file(dataFile);
  std::string base_name = input_file.substr(0, input_file.rfind('.'));
  std::string extension = input_file.substr(input_file.rfind('.')+1);
  if (extension.compare("mps")) {
    dcoMessageHandler_->message(DISCO_READ_MPSFILEONLY,
                                *dcoMessages_) << CoinMessageEol;
  }

  // read mps file
  CoinMpsIO * reader = new CoinMpsIO;

  // get log level parameters
  int dcoLogLevel =  dcoPar_->entry(DcoParams::logLevel);
  reader->messageHandler()->setLogLevel(dcoLogLevel);
  reader->readMps(dataFile, "");
  numCols_ = reader->getNumCols();
  numLinearRows_ = reader->getNumRows();
  matrix_ = new CoinPackedMatrix(*(reader->getMatrixByCol()));

  // == allocate variable bounds
  colLB_ = new double [numCols_];
  colUB_ = new double [numCols_];

  // == allocate row bounds
  rowLB_ = new double [numLinearRows_];
  rowUB_ = new double [numLinearRows_];

  // == copy bounds
  std::copy(reader->getColLower(), reader->getColLower()+numCols_, colLB_);
  std::copy(reader->getColUpper(), reader->getColUpper()+numCols_, colUB_);
  std::copy(reader->getRowLower(), reader->getRowLower()+numLinearRows_, rowLB_);
  std::copy(reader->getRowUpper(), reader->getRowUpper()+numLinearRows_, rowUB_);

  // == set objective sense
  // todo(aykut) we should ask reader about the objective sense
  objSense_ = dcoPar_->entry(DcoParams::objSense);

  // == allocate objective coefficients
  objCoef_ = new double [numCols_];
  double const * reader_obj = reader->getObjCoefficients();
  if (objSense_ > 0.0) {
    std::copy(reader_obj, reader_obj+numCols_, objCoef_);
  }
  else {
    for (int i = 0; i<numCols_; ++i) {
      objCoef_[i] = -reader_obj[i];
    }
  }
  // == load data to solver
  solver_->loadProblem(*matrix_, colLB_, colUB_, objCoef_,
                       rowLB_, rowUB_);
  // == add variables to the model
  readAddVariables(reader);
  // == add linear constraints to the model
  readAddLinearConstraints(reader);
  // == add conic constraints to the model
  readAddConicConstraints(reader);
  delete reader;
}


void DcoModel::readParameters(const int argnum,
                              const char * const * arglist) {
  AlpsPar()->readFromArglist(argnum, arglist);
  dcoPar_->readFromArglist(argnum, arglist);
}

// Add variables to *this.
void DcoModel::readAddVariables(CoinMpsIO * reader) {
  // get variable integrality constraints
  numIntegerCols_ = 0;
  DcoIntegralityType * i_type = new DcoIntegralityType[numCols_];

  // may return NULL, especially when there are no integer variables.
  char const * is_integer = reader->integerColumns();
  if (is_integer!=NULL) {
    for (int i=0; i<numCols_; ++i) {
      if (is_integer[i]) {
        i_type[i] = DcoIntegralityTypeInt;
        numIntegerCols_++;
      }
      else {
        i_type[i] = DcoIntegralityTypeCont;
      }
    }
  }
  else {
    dcoMessageHandler_->message(3000, "Dco",
                                "CoinMpsIO::integerColumns() did return "
                                "NULL pointer for "
                                " column types! This looks like a CoinMpsIO "
                                "bug to me. Please report! I will use "
                                " CoinMpsIO::isContinuous(int index) function "
                                "instead.",
                                'W', 0)
      << CoinMessageEol;
    for (int i=0; i<numCols_; ++i) {
      if (reader->isContinuous(i)) {
        i_type[i] = DcoIntegralityTypeCont;
      }
      else {
        i_type[i] = DcoIntegralityTypeInt;
        numIntegerCols_++;
      }
    }
  }
  // add variables
  BcpsVariable ** variables = new BcpsVariable*[numCols_];
  for (int i=0; i<numCols_; ++i) {
    variables[i] = new DcoVariable(i, colLB_[i], colUB_[i], colLB_[i],
                                   colLB_[i], i_type[i]);
  }
  setVariables(variables, numCols_);
  // variables[i] are now owned by BcpsModel, do not free them.
  delete[] variables;
  delete[] i_type;
}

void DcoModel::readAddLinearConstraints(CoinMpsIO * reader) {
  // == add constraints to *this
  BcpsConstraint ** constraints = new BcpsConstraint*[numLinearRows_];
  CoinPackedMatrix const * matrix = reader->getMatrixByRow();
  int const * indices = matrix->getIndices();
  double const * values = matrix->getElements();
  int const * lengths = matrix->getVectorLengths();
  int const * starts = matrix->getVectorStarts();
  for (int i=0; i<numLinearRows_; ++i) {
    constraints[i] = new DcoLinearConstraint(lengths[i], indices+starts[i],
                                             values+starts[i], rowLB_[i],
                                             rowUB_[i]);
  }
  setConstraints(constraints, numLinearRows_);
  // constraints[i] are owned by BcpsModel. Do not free them here.
  delete[] constraints;
}

void DcoModel::readAddConicConstraints(CoinMpsIO * reader) {
  int nOfCones = 0;
  int * coneStart = NULL;
  int * coneMembers = NULL;
  int * coneType = NULL;
  int reader_return = reader->readConicMps(NULL, coneStart, coneMembers,
                                       coneType, nOfCones);
  // when there is no conic section status is -3.
  if (reader_return==-3) {
    dcoMessageHandler_->message(DISCO_READ_NOCONES,
                                *dcoMessages_);
  }
  else if (reader_return!=0) {
    dcoMessageHandler_->message(DISCO_READ_MPSERROR,
                                *dcoMessages_) << reader_return
                                              << CoinMessageEol;
  }
  // store number of cones in the problem
  numConicRows_ = nOfCones;
  // log cone information messages
  if (nOfCones) {
    dcoMessageHandler_->message(DISCO_READ_CONESTATS1,
                                *dcoMessages_) << nOfCones
                                               << CoinMessageEol;
    for (int i=0; i<nOfCones; ++i) {
      dcoMessageHandler_->message(DISCO_READ_CONESTATS2,
                                  *dcoMessages_)
        << i
        << coneStart[i+1] - coneStart[i]
        << coneType[i]
        << CoinMessageEol;
    }
  }
  // iterate over cones and add them to the model
  for (int i=0; i<nOfCones; ++i) {
    if (coneType[i]!=1 and coneType[i]!=2) {
      dcoMessageHandler_->message(DISCO_READ_CONEERROR,
                                  *dcoMessages_) << CoinMessageEol;
    }
    int num_members = coneStart[i+1]-coneStart[i];
    if (coneType[i]==2 and num_members<3) {
      dcoMessageHandler_->message(DISCO_READ_ROTATEDCONESIZE,
                                  *dcoMessages_) << CoinMessageEol;
    }
    DcoLorentzConeType type;
    if (coneType[i]==1) {
      type = DcoLorentzCone;
    }
    else if (coneType[i]==2) {
      type = DcoRotatedLorentzCone;
    }
    addConstraint(new DcoConicConstraint(type, num_members,
                                         coneMembers+coneStart[i]));
    // cone_[i] = new OsiLorentzCone(type, num_members,
    //                            coneMembers+coneStart[i]);
#ifndef __OA__
    OsiLorentzConeType osi_type;
    if (coneType[i]==1) {
      osi_type = OSI_QUAD;
    }
    else if (coneType[i]==2) {
      osi_type = OSI_RQUAD;
    }
    solver_->addConicConstraint(osi_type, coneStart[i+1]-coneStart[i],
                                coneMembers+coneStart[i]);
#endif
  }
  delete[] coneStart;
  delete[] coneMembers;
  delete[] coneType;
  // update rowLB_ and rowUB_, add conic row bounds
  double * temp = rowLB_;
  rowLB_ = new double[numLinearRows_+numConicRows_];
  std::copy(temp, temp+numLinearRows_, rowLB_);
  std::fill_n(rowLB_+numLinearRows_, numConicRows_, 0.0);
  delete[] temp;
  temp = rowUB_;
  rowUB_ = new double[numLinearRows_+numConicRows_];
  std::copy(temp, temp+numLinearRows_, rowUB_);
  std::fill_n(rowUB_+numLinearRows_, numConicRows_, DISCO_INFINITY);
  delete[] temp;
}

void DcoModel::preprocess() {
#ifdef __OA__
  // approximateCones();
  // // update row information
  // numLinearRows_ = solver_->getNumRows();
  // // update row bounds
  // delete[] rowLB_;
  // delete[] rowUB_;
  // int numLinearRows_ = solver_->getNumRows();
  // rowLB_ = new double [numLinearRows_];
  // rowUB_ = new double [numLinearRows_];
  // std::copy(solver_->getRowLower(),
  //        solver_->getRowLower()+numLinearRows_,
  //        rowLB_);
  // std::copy(solver_->getRowUpper(),
  //        solver_->getRowUpper()+numLinearRows_,
  //        rowUB_);
#endif
}

void DcoModel::approximateCones() {
  dcoMessageHandler_->message(DISCO_NOT_IMPLEMENTED, *dcoMessages_)
    << __FILE__ << __LINE__ << CoinMessageEol;
  throw std::exception();
}

//todo(aykut) why does this return to bool?
// should be fixed in Alps level.
bool DcoModel::setupSelf() {

  // set integer column indices
  std::vector<BcpsVariable*> & cols = getVariables();
  int numCols = getNumCoreVariables();
  integerCols_ = new int[numIntegerCols_];
  for (int i=0, k=0; i<numCols; ++i) {
    BcpsIntegral_t i_type = cols[i]->getIntType();
    if (i_type=='I' or i_type=='B') {
      integerCols_[k] = i;
      k++;
    }
  }
  // set relaxed array for integer columns
  numRelaxedCols_ = numIntegerCols_;
  relaxedCols_ = new int[numRelaxedCols_];
  std::copy(integerCols_, integerCols_+numIntegerCols_,
            relaxedCols_);
#if defined(__OA__)
  // we relax conic constraint too when OA is used.
  // set relaxed array for conic constraints
  numRelaxedRows_ = numConicRows_;
  relaxedRows_ = new int[numRelaxedRows_];
  // todo(aykut) we assume conic rows start after linear rows.
  // if not iterate over rows and determine their type (DcoConstraint::type())
  for (int i=0; i<numRelaxedRows_; ++i) {
    relaxedRows_[i] = numLinearRows_+i;
  }
#endif
  // set message levels
  setMessageLevel();

  // set branch strategy
  setBranchingStrategy();

  // add constraint generators
  addConstraintGenerators();

  return true;
}

// set message level
void DcoModel::setMessageLevel() {
  // get Alps log level
  int alps_log_level = AlpsPar()->entry(AlpsParams::msgLevel);
  int dco_log_level = dcoPar_->entry(DcoParams::logLevel);

#if defined(DISCO_DEBUG) || defined(DISCO_DEBUG_BRANCH) ||defined(DISCO_DEBUG_CUT) || defined(DISCO_DEBUG_PROCESS)
  // reset Disco log level, we will set it depending on the debug macros are
  // defined.
  dco_log_level = 0;
#endif

#ifdef DISCO_DEBUG_BRANCH
  dco_log_level += DISCO_DLOG_BRANCH;
#endif

#ifdef DISCO_DEBUG_CUT
  dco_log_level += DISCO_DLOG_CUT;
#endif

#ifdef DISCO_DEBUG_PROCESS
  dco_log_level += DISCO_DLOG_PROCESS;
#endif

#ifdef DISCO_DEBUG
  // debug branch, cut, process
  dco_log_level = DISCO_DLOG_BRANCH;
  dco_log_level += DISCO_DLOG_CUT;
  dco_log_level += DISCO_DLOG_PROCESS;
#endif

  // todo(aykut) create different parameters for Alps and Bcps.
  getKnowledgeBroker()->messageHandler()->setLogLevel(alps_log_level);
  bcpsMessageHandler()->setLogLevel(alps_log_level);
  dcoMessageHandler_->setLogLevel(dco_log_level);


  // dynamic_cast<OsiClpSolverInterface*>(solver)->setHintParam(OsiDoReducePrint,false,OsiHintDo, 0);
  solver_->setHintParam(OsiDoReducePrint,false,OsiHintDo, 0);
}

void DcoModel::addConstraintGenerators() {
  // get cut strategy
  cutStrategy_ = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutStrategy));
  // get cut generation frequency
  cutGenerationFrequency_ = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutGenerationFrequency));
  if (cutGenerationFrequency_ < 1) {
    // invalid cut fraquency given, change it to 1.
    dcoMessageHandler_->message(DISCO_INVALID_CUT_FREQUENCY,
                                *dcoMessages_)
      << cutGenerationFrequency_
      << 1
      << CoinMessageEol;
    cutGenerationFrequency_ = 1;
  }

  // get cut strategies from parameters
  DcoCutStrategy cliqueStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutCliqueStrategy));
  DcoCutStrategy fCoverStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutFlowCoverStrategy));
  DcoCutStrategy gomoryStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutGomoryStrategy));
  DcoCutStrategy knapStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutKnapsackStrategy));
  DcoCutStrategy mirStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutMirStrategy));
  DcoCutStrategy oddHoleStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutOddHoleStrategy));
  DcoCutStrategy probeStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutProbingStrategy));
  DcoCutStrategy twoMirStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutTwoMirStrategy));
  DcoCutStrategy ipmStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutIpmStrategy));
  DcoCutStrategy ipmintStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutIpmIntStrategy));
  DcoCutStrategy oaStrategy = static_cast<DcoCutStrategy>
    (dcoPar_->entry(DcoParams::cutOaStrategy));

  // get cut frequencies from parameters
  int cliqueFreq = dcoPar_->entry(DcoParams::cutCliqueFreq);
  int fCoverFreq = dcoPar_->entry(DcoParams::cutFlowCoverFreq);
  int gomoryFreq = dcoPar_->entry(DcoParams::cutGomoryFreq);
  int knapFreq = dcoPar_->entry(DcoParams::cutKnapsackFreq);
  int mirFreq = dcoPar_->entry(DcoParams::cutMirFreq);
  int oddHoleFreq = dcoPar_->entry(DcoParams::cutOddHoleFreq);
  int probeFreq = dcoPar_->entry(DcoParams::cutProbingFreq);
  int twoMirFreq = dcoPar_->entry(DcoParams::cutTwoMirFreq);
  int ipmFreq = dcoPar_->entry(DcoParams::cutIpmFreq);
  int ipmintFreq = dcoPar_->entry(DcoParams::cutIpmIntFreq);
  int oaFreq = dcoPar_->entry(DcoParams::cutOaFreq);

  //----------------------------------
  // Add cut generators.
  //----------------------------------
  // Add probe cut generator
  if (probeStrategy == DcoCutStrategyNotSet) {
    // Disable by default
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      probeStrategy = DcoCutStrategyNone;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      probeStrategy = cutStrategy_;
      probeFreq = cutGenerationFrequency_;
    }
    else {
      probeStrategy = cutStrategy_;
    }
  }
  if (probeStrategy != DcoCutStrategyNone) {
    CglProbing *probing = new CglProbing;
    probing->setUsingObjective(true);
    probing->setMaxPass(1);
    probing->setMaxPassRoot(5);
    // Number of unsatisfied variables to look at
    probing->setMaxProbe(10);
    probing->setMaxProbeRoot(1000);
    // How far to follow the consequences
    probing->setMaxLook(50);
    probing->setMaxLookRoot(500);
    // Only look at rows with fewer than this number of elements
    probing->setMaxElements(200);
    probing->setRowCuts(3);
    addConGenerator(probing, "Probing", probeStrategy, probeFreq);
  }

  // Add clique cut generator.
  if (cliqueStrategy == DcoCutStrategyNotSet) {
    // Only at root by default
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      cliqueStrategy = DcoCutStrategyRoot;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      cliqueFreq = cutGenerationFrequency_;
      cliqueStrategy = DcoCutStrategyPeriodic;
    }
    else { // Root or Auto
      cliqueStrategy = cutStrategy_;
    }
  }
  if (cliqueStrategy != DcoCutStrategyNone) {
    CglClique *cliqueCut = new CglClique ;
    cliqueCut->setStarCliqueReport(false);
    cliqueCut->setRowCliqueReport(false);
    addConGenerator(cliqueCut, "Clique", cliqueStrategy, cliqueFreq);
  }

  // Add odd hole cut generator.
  if (oddHoleStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      // Disable by default
      oddHoleStrategy = DcoCutStrategyNone;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      oddHoleStrategy = DcoCutStrategyPeriodic;
      oddHoleFreq = cutGenerationFrequency_;
    }
    else {
      oddHoleStrategy = cutStrategy_;
    }
  }
  if (oddHoleStrategy != DcoCutStrategyNone) {
    CglOddHole *oldHoleCut = new CglOddHole;
    oldHoleCut->setMinimumViolation(0.005);
    oldHoleCut->setMinimumViolationPer(0.00002);
    // try larger limit
    oldHoleCut->setMaximumEntries(200);
    addConGenerator(oldHoleCut, "OddHole", oddHoleStrategy, oddHoleFreq);
  }

  // Add flow cover cut generator.
  if (fCoverStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      fCoverStrategy = DcoCutStrategyAuto;
      fCoverFreq = cutGenerationFrequency_;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      fCoverStrategy = cutStrategy_;
      fCoverFreq = cutGenerationFrequency_;
    }
    else {
      fCoverStrategy = cutStrategy_;
    }
  }
  if (fCoverStrategy != DcoCutStrategyNone) {
    CglFlowCover *flowGen = new CglFlowCover;
    addConGenerator(flowGen, "Flow Cover", fCoverStrategy, fCoverFreq);
  }

  // Add knapsack cut generator.
  if (knapStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      // Only at root by default
      knapStrategy = DcoCutStrategyRoot;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      knapStrategy = cutStrategy_;
      knapFreq = cutGenerationFrequency_;
    }
    else {
      knapStrategy = cutStrategy_;
    }
  }
  if (knapStrategy != DcoCutStrategyNone) {
    CglKnapsackCover *knapCut = new CglKnapsackCover;
    addConGenerator(knapCut, "Knapsack", knapStrategy, knapFreq);
  }

  // Add MIR cut generator.
  if (mirStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      // Disable by default
      mirStrategy = DcoCutStrategyNone;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      mirStrategy = cutStrategy_;
      mirFreq = cutGenerationFrequency_;
    }
    else {
      mirStrategy = cutStrategy_;
    }
  }
  if (mirStrategy != DcoCutStrategyNone) {
    CglMixedIntegerRounding2 *mixedGen = new CglMixedIntegerRounding2;
    addConGenerator(mixedGen, "MIR", mirStrategy, mirFreq);
  }

  // Add Gomory cut generator.
  if (gomoryStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      // Only at root by default
      gomoryStrategy = DcoCutStrategyRoot;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      gomoryStrategy = cutStrategy_;
      gomoryFreq = cutGenerationFrequency_;
    }
    else {
      gomoryStrategy = cutStrategy_;
    }
  }
  if (gomoryStrategy != DcoCutStrategyNone) {
    CglGomory *gomoryCut = new CglGomory;
    // try larger limit
    gomoryCut->setLimit(300);
    addConGenerator(gomoryCut, "Gomory", gomoryStrategy, gomoryFreq);
  }

  // Add Tow MIR cut generator.
  // Disable forever, not useful.
  twoMirStrategy = DcoCutStrategyNone;
  if (twoMirStrategy != DcoCutStrategyNone) {
    CglTwomir *twoMirCut =  new CglTwomir;
    addConGenerator(twoMirCut, "Two MIR", twoMirStrategy, twoMirFreq);
  }

  // Add IPM cut generator
  if (ipmStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      // Only at root by default
      ipmStrategy = DcoCutStrategyRoot;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      ipmStrategy = cutStrategy_;
      ipmFreq = cutGenerationFrequency_;
    }
    else {
      ipmStrategy = cutStrategy_;
    }
  }
  if (ipmStrategy != DcoCutStrategyNone) {
    CglConicCutGenerator * ipm_gen = new CglConicIPM();
    addConGenerator(ipm_gen, "IPM", ipmStrategy, ipmFreq);
  }

  // Add IPM integer cut generator
  if (ipmintStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      // Only at root by default
      ipmintStrategy = DcoCutStrategyRoot;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      ipmintStrategy = cutStrategy_;
      ipmintFreq = cutGenerationFrequency_;
    }
    else {
      ipmintStrategy = cutStrategy_;
    }
  }
  if (ipmintStrategy != DcoCutStrategyNone) {
    CglConicCutGenerator * ipm_int_gen = new CglConicIPMint();
    addConGenerator(ipm_int_gen, "IPMint", ipmintStrategy, ipmintFreq);
  }

  // Add Outer approximation cut generator
  if (oaStrategy == DcoCutStrategyNotSet) {
    if (cutStrategy_ == DcoCutStrategyNotSet) {
      // periodic by default
      oaStrategy = DcoCutStrategyPeriodic;
    }
    else if (cutStrategy_ == DcoCutStrategyPeriodic) {
      oaStrategy = cutStrategy_;
      oaFreq = cutGenerationFrequency_;
    }
    else {
      oaStrategy = cutStrategy_;
    }
  }
  if (oaStrategy != DcoCutStrategyNone) {
    CglConicCutGenerator * oa_gen = new CglConicOA();
    addConGenerator(oa_gen, "OA", oaStrategy, oaFreq);
  }

  // Adjust cutStrategy_ according to the strategies of each cut generators.
  // set it to the most allowing one.
  // if there is at least one periodic strategy, set it to periodic.
  // if no periodic and there is at least one root, set it to root
  // set it to None otherwise.
  cutStrategy_ = DcoCutStrategyNone;
  cutGenerationFrequency_ = 100;
  bool periodic_exists = false;
  bool root_exists = false;
  std::vector<DcoConGenerator*>::iterator it;
  for (it=conGenerators_.begin(); it!=conGenerators_.end(); ++it) {
    DcoCutStrategy curr = (*it)->strategy();
    if (curr==DcoCutStrategyPeriodic) {
      periodic_exists = true;
      break;
    }
    else if (curr==DcoCutStrategyRoot) {
      root_exists = true;
    }
  }
  if (periodic_exists) {
    cutStrategy_ = DcoCutStrategyPeriodic;
    cutGenerationFrequency_ = 1;
  }
  else if (root_exists) {
    cutStrategy_ = DcoCutStrategyRoot;
    // this is not relevant, since we will generate only in root.
    cutGenerationFrequency_ = 100;
  }
}


void DcoModel::addConGenerator(CglCutGenerator * cgl_gen,
                               char const * name,
                               DcoCutStrategy dco_strategy,
                               int frequency) {
  DcoConGenerator * con_gen = new DcoLinearConGenerator(this, cgl_gen, name,
                                                        dco_strategy,
                                                        frequency);
  conGenerators_.push_back(con_gen);
}

/// Add constraint generator.
void DcoModel::addConGenerator(CglConicCutGenerator * cgl_gen,
                               char const * name,
                               DcoCutStrategy dco_strategy,
                               int frequency) {
  DcoConGenerator * con_gen = new DcoConicConGenerator(this, cgl_gen, name,
                                                       dco_strategy,
                                                       frequency);
  conGenerators_.push_back(con_gen);
}


void DcoModel::setBranchingStrategy() {
    // set branching startegy
  int brStrategy = dcoPar_->entry(DcoParams::branchStrategy);
  switch(brStrategy) {
  case DcoBranchingStrategyMaxInfeasibility:
    branchStrategy_ = new DcoBranchStrategyMaxInf(this);
    break;
  case DcoBranchingStrategyPseudoCost:
    branchStrategy_ = new DcoBranchStrategyPseudo(this);
    break;
  // case DcoBranchingStrategyReliability:
  //   branchStrategy_ = new DcoBranchStrategyRel(this, reliability);
  //   break;
  // case DcoBranchingStrategyStrong:
  //   branchStrategy_ = new DcoBranchStrategyStrong(this);
  //   break;
  // case DcoBranchingStrategyBilevel:
  //   branchStrategy_ = new DcoBranchStrategyBilevel(this);
  //   break;
  default:
    dcoMessageHandler_->message(DISCO_UNKNOWN_BRANCHSTRATEGY,
                                *dcoMessages_) << brStrategy << CoinMessageEol;
    throw CoinError("Unknown branch strategy.", "setupSelf","DcoModel");
  }

  // set ramp up branch strategy
  brStrategy = dcoPar_->entry(DcoParams::branchStrategyRampUp);
  switch(brStrategy) {
  case DcoBranchingStrategyMaxInfeasibility:
    rampUpBranchStrategy_ = new DcoBranchStrategyMaxInf(this);
    break;
  // case DcoBranchingStrategyPseudoCost:
  //   rampUpBranchStrategy_ = new DcoBranchStrategyPseudo(this, 1);
  //   break;
  // case DcoBranchingStrategyReliability:
  //   rampUpBranchStrategy_ = new DcoBranchStrategyRel(this, reliability);
  //   break;
  // case DcoBranchingStrategyStrong:
  //   rampUpBranchStrategy_ = new DcoBranchStrategyStrong(this);
  //   break;
  // case DcoBranchingStrategyBilevel:
  //   rampUpBranchStrategy_ = new DcoBranchStrategyBilevel(this);
  //   break;
  default:
    dcoMessageHandler_->message(DISCO_UNKNOWN_BRANCHSTRATEGY,
                                *dcoMessages_) << brStrategy << CoinMessageEol;
    throw std::exception();
    throw CoinError("Unknown branch strategy.", "setupSelf","DcoModel");
  }
}

void DcoModel::postprocess() {
}


AlpsTreeNode * DcoModel::createRoot() {
  DcoTreeNode * root = new DcoTreeNode();
  DcoNodeDesc * desc = new DcoNodeDesc(this);
  root->setDesc(desc);
  std::vector<BcpsVariable *> & cols = getVariables();
  std::vector<BcpsConstraint *> & rows = getConstraints();
  int numCols = getNumCoreVariables();
  int numRows = getNumCoreConstraints();
  int * varIndices1 = new int [numCols];
  int * varIndices2 = new int [numCols];
  int * varIndices3 = NULL;
  int * varIndices4 = NULL;
  double * vlhe = new double [numCols];
  double * vuhe = new double [numCols];
  double * vlse = NULL;
  double * vuse = NULL;
  int * conIndices1 = new int [numRows];
  int * conIndices2 = new int [numRows];
  int * conIndices3 = NULL;
  int * conIndices4 = NULL;
  double * clhe = new double [numRows];
  double * cuhe = new double [numRows];
  double * clse = NULL;
  double * cuse = NULL;
  //-------------------------------------------------------------
  // Get var bounds and indices.
  //-------------------------------------------------------------
  for (int i=0; i<numCols; ++i) {
    vlhe[i] = cols[i]->getLbHard();
    vuhe[i] = cols[i]->getUbHard();
    varIndices1[i] = i;
    varIndices2[i] = i;
  }
  //-------------------------------------------------------------
  // Get con bounds and indices.
  //-------------------------------------------------------------
  for (int i=0; i<numRows; ++i) {
    clhe[i] = rows[i]->getLbHard();
    cuhe[i] = rows[i]->getUbHard();
    conIndices1[i] = i;
    conIndices2[i] = i;
  }
  int * tempInd = NULL;
  BcpsObject ** tempObj = NULL;
  desc->assignVars(0, tempInd,
                   0, tempObj,
                   false, numCols, varIndices1, vlhe, /*Var hard lb*/
                   false, numCols, varIndices2, vuhe, /*Var hard ub*/
                   false, 0, varIndices3, vlse,       /*Var soft lb*/
                   false, 0, varIndices4, vuse);      /*Var soft ub*/
  desc->assignCons(0, tempInd,
                   0, tempObj,
                   false, numRows, conIndices1, clhe, /*Con hard lb*/
                   false, numRows, conIndices2, cuhe, /*Con hard ub*/
                   false, 0,conIndices3,clse,         /*Con soft lb*/
                   false, 0,conIndices4,cuse);        /*Con soft ub*/
  //-------------------------------------------------------------
  // Mark it as an explicit node.
  //-------------------------------------------------------------
  root->setExplicit(1);
  return root;
}


DcoSolution * DcoModel::feasibleSolution(int & numInfColumns,
                                         int & numInfRows) {
  // set stats to 0
  numInfColumns = 0;
  numInfRows = 0;

  // check feasibility of relxed columns, ie. integrality constraints
  // get vector of variables
  std::vector<BcpsVariable*> & cols = getVariables();
  for (int i=0; i<numRelaxedCols_; ++i) {
    // get column relaxedCol_[i]
    DcoVariable * curr = dynamic_cast<DcoVariable*> (cols[relaxedCols_[i]]);
    // check feasibility
    int preferredDir;
    double infeas = curr->infeasibility(this, preferredDir);
    if (infeas>0) {
      numInfColumns++;
    }
  }

  // check feasibility of relaxed rows
  // get vector of constraints
  std::vector<BcpsConstraint*> & rows = getConstraints();
  for (int i=0; i<numRelaxedRows_; ++i) {
    // get row relaxedRows_[i]
    DcoConstraint * curr = dynamic_cast<DcoConstraint*> (rows[relaxedRows_[i]]);
    // check feasibility
    int preferredDir;
    double infeas = curr->infeasibility(this, preferredDir);
    if (infeas>0) {
      numInfRows++;
    }
  }

  // create DcoSolution instance if feasbile
  DcoSolution * dco_sol = 0;
  if (numInfColumns==0 && numInfRows==0) {
    double const * sol = solver()->getColSolution();
    double quality = solver()->getObjValue();
    dco_sol = new DcoSolution(numCols_, sol, quality);

    // debug stuff
    std::stringstream debug_msg;
    debug_msg << "Solution found. ";
    debug_msg << "Obj value ";
    debug_msg << quality;
    dcoMessageHandler_->message(0, "Dco", debug_msg.str().c_str(),
                                'G', DISCO_DLOG_CUT)
      << CoinMessageEol;
    // end of debug stuff

  }
  return dco_sol;
}

// todo(aykut) why does this return int?
// todo(aykut) what if objsense is -1 and problem is maximization?
int DcoModel::storeSolution(DcoSolution * sol) {
  double quality = sol->getQuality();
  // Update cutoff and lp cutoff.
  double obj_tol = dcoPar_->entry(DcoParams::objTol);
  if (quality+obj_tol < upperBound_) {
    // Store in Alps pool, assumes minimization.
    getKnowledgeBroker()->addKnowledge(AlpsKnowledgeTypeSolution,
                                       sol,
                                       objSense_ * quality);
  }
  return AlpsReturnStatusOk;
}
