ATS: The Advanced Terrestrial Simulator
=======================================

The Advanced Terrestrial Simulator (formerly sometimes known as the Arctic Terrestrial Simulator) is a code for solving ecosystem-based, integrated, distributed hydrology.

Capabilities are largely based on solving various forms of Richards equation coupled to a surface flow equation, along with the needed sources and sinks for ecosystem and climate models.  This can (but need not) include thermal processes (especially ice for frozen soils), evapo-transpiration, albedo-driven surface energy balances, snow, biogeochemistry, plant dynamics, deformation, transport, and much more.  In addition, we solve problems of reactive transport in both the subsurface and surface, leveraging external geochemical engines through the Alquimia interface.

Join the Community
------------------

ATS is more than just a code, but a community of users with a lot of experience in both integrated hydrology, Arctic hydrology, reactive transport, and ATS code development.  Please join us on our [google group](https://groups.google.com/forum/#!forum/ats-users).  We try very hard to create a welcoming community that supports and enables our users to do their science.


Installation
------------

ATS is now built as a part of Amanzi directly. Please see the [ATS installation instructions](https://github.com/amanzi/amanzi/blob/master/INSTALL_ATS.md) on Amanzi's site.

Documentation
-------------

Our [Documentation](https://amanzi.github.io/ats/) covers the input spec, and is motivated by a large suite of [Demos](https://github.com/amanzi/ats-demos).

See also our [Wiki](https://github.com/amanzi/ats/wiki) and [Frequently Asked Questions](https://github.com/amanzi/ats/wiki/FAQs), or take our online [Short Course](https://github.com/amanzi/ats-short-course).


License and Copyright
---------------------

Please see the [LICENSE](https://github.com/amanzi/ats/blob/master/LICENSE) and [COPYRIGHT](https://github.com/amanzi/ats/blob/master/COPYRIGHT) files included in the top level directory of your ATS download.

Citation
--------

In all works, please cite the code:

E.T. Coon, M. Berndt, A. Jan, D. Svyatsky, A.L. Atchley, E. Kikinzon, D.R. Harp, G. Manzini, E. Shelef, K. Lipnikov, R. Garimella, C. Xu, J.D. Moulton, S. Karra, S.L. Painter, E. Jafarov, and S. Molins. 2020. Advanced Terrestrial Simulator. U.S. Department of Energy, USA. Version 1.0. [DOI](https://doi.org/10.11578/dc.20190911.1)

Additionally, consider citing one or more of the below, depending upon the application space:

**Watershed Hydrology:** Coon, Ethan T., et al. "Coupling surface flow and subsurface flow in complex soil structures using mimetic finite differences." Advances in Water Resources 144 (2020): 103701. [DOI](https://doi.org/10.1016/j.advwatres.2020.103701)

**Arctic Hydrology:** Painter, Scott L., et al. "Integrated surface/subsurface permafrost thermal hydrology: Model formulation and proof‐of‐concept simulations." Water Resources Research 52.8 (2016): 6062-6077. [DOI](https://doi.org/10.1002/2015WR018427)

**Reactive Transport:**  Painter, Scott L., et al. "Integrated surface/subsurface permafrost thermal hydrology: Model formulation and proof‐of‐concept simulations." Water Resources Research 52.8 (2016): 6062-6077. [DOI](https://doi.org/10.1029/2022WR032074)

**Multiphysics Modeling:** Coon, Ethan T., J. David Moulton, and Scott L. Painter. "Managing complexity in simulations of land surface and near-surface processes." Environmental modelling & software 78 (2016): 134-149. [DOI](https://doi.org/10.1016/j.envsoft.2015.12.017)
