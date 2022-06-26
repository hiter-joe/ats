/* -*-  mode: c++; indent-tabs-mode: nil -*- */

// -----------------------------------------------------------------------------
// ATS
//
// License: see $ATS_DIR/COPYRIGHT
// Author: Ethan Coon (ecoon@lanl.gov)
//
// Scheme for taking coefficients for div-grad operators from cells to
// faces.
// -----------------------------------------------------------------------------

#ifndef AMANZI_UPWINDING_ARITHMETICMEAN_SCHEME_
#define AMANZI_UPWINDING_ARITHMETICMEAN_SCHEME_

#include "Key.hh"
#include "Tag.hh"
#include "upwinding.hh"

namespace Amanzi {
namespace Operators {

class UpwindArithmeticMean : public Upwinding {
 public:
  UpwindArithmeticMean(const std::string& pkname, const Tag& tag);

  virtual void Update(const CompositeVector& cells,
                      CompositeVector& faces,
                      const State& S,
                      const Teuchos::Ptr<Debugger>& db=Teuchos::null) const override;

  void CalculateCoefficientsOnFaces(
        const CompositeVector& cell_coef,
        CompositeVector& face_coef) const;

  virtual void
  UpdateDerivatives(const Teuchos::Ptr<State>& S,
                    Key potential_key,
                    const CompositeVector& dconductivity,
                    const std::vector<int>& bc_markers,
                    const std::vector<double>& bc_values,
                    std::vector<Teuchos::RCP<Teuchos::SerialDenseMatrix<int, double> > >* Jpp_faces) const override;

  virtual std::string
  CoefficientLocation() const override { return "upwind: face"; }

 private:
  std::string pkname_;
  Tag tag_;
};

} // namespace
} // namespace

#endif
