/*===========================================================================*
 * This file is part of the Discrete Conic Optimization (DisCO) Solver.      *
 *                                                                           *
 * DisCO is distributed under the Eclipse Public License as part of the      *
 * COIN-OR repository (http://www.coin-or.org).                              *
 *                                                                           *
 * Authors:                                                                  *
 *          Aykut Bulut, Lehigh University                                   *
 *          Yan Xu, Lehigh University                                        *
 *          Ted Ralphs, Lehigh University                                    *
 *                                                                           *
 * Copyright (C) 2001-2018, Lehigh University, Aykut Bulut, Yan Xu, and      *
 * Ted Ralphs. All Rights Reserved.                                          *
 *===========================================================================*/


#ifndef DcoSubTree_hpp_
#define DcoSubTree_hpp_

#include <BcpsSubTree.h>

class DcoSubTree: public BcpsSubTree {
public:
  DcoSubTree();
  virtual ~DcoSubTree();
};

#endif
