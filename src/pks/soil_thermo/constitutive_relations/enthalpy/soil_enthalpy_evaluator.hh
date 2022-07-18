/* -*-  mode: c++; indent-tabs-mode: nil -*- */

/* -----------------------------------------------------------------------------
ATS

Authors: Ethan Coon (ecoon@lanl.gov)

Evaluator for enthalpy.
----------------------------------------------------------------------------- */


#ifndef AMANZI_SOIL_ENTHALPY_EVALUATOR_HH_
#define AMANZI_SOIL_ENTHALPY_EVALUATOR_HH_

#include "Teuchos_ParameterList.hpp"

#include "Factory.hh"
#include "EvaluatorSecondaryMonotype.hh"

namespace Amanzi {
namespace SoilThermo {

class SoilEnthalpyEvaluator : public EvaluatorSecondaryMonotypeCV {

 public:
  explicit
  SoilEnthalpyEvaluator(Teuchos::ParameterList& plist);
  SoilEnthalpyEvaluator(const SoilEnthalpyEvaluator& other);

  virtual Teuchos::RCP<Evaluator> Clone() const override;

  // Required methods from SecondaryVariableEvaluator
  virtual void Evaluate_(const State& S,
      const std::vector<CompositeVector*>& result) override;
  virtual void EvaluatePartialDerivative_(const State& S,
      const Key& wrt_key, const Tag& wrt_tag,
      const std::vector<CompositeVector*>& result) override;

 protected:

  Key pres_key_;
  Key dens_key_;
  Key ie_key_;
  bool include_work_;

 private:
  static Utils::RegisteredFactory<Evaluator,SoilEnthalpyEvaluator> factory_;

};

} // namespace
} // namespace

#endif
