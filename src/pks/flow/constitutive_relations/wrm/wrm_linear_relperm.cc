/*
  Copyright 2010-202x held jointly by participating institutions.
  ATS is released under the three-clause BSD License.
  The terms of use and "as is" disclaimer for this license are
  provided in the top-level COPYRIGHT file.

  Authors: Ethan Coon (ecoon@lanl.gov)
*/

/*
  WRM which calls another WRM for saturation but sets 0 rel perm.

*/

#include "dbc.hh"
#include "wrm_linear_relperm.hh"

namespace Amanzi {
namespace Flow {

WRMLinearRelPerm::WRMLinearRelPerm(Teuchos::ParameterList& plist) : plist_(plist)
{
  InitializeFromPlist_();
};


void
WRMLinearRelPerm::InitializeFromPlist_()
{
  Teuchos::ParameterList& sublist = plist_.sublist("WRM parameters");
  sublist.remove("model type", false);

  WRMFactory fac;
  wrm_ = fac.createWRM(sublist);
};


} // namespace Flow
} // namespace Amanzi
