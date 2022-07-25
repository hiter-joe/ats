/* -*-  mode: c++; indent-tabs-mode: nil -*- */
/* -------------------------------------------------------------------------
ATS

License: see $ATS_DIR/COPYRIGHT
Author: Ethan Coon

Implementation for the Coordinator.  Coordinator is basically just a class to hold
the cycle driver, which runs the overall, top level timestep loop.  It
instantiates states, ensures they are initialized, and runs the timestep loop
including Vis and restart/checkpoint dumps.  It contains one and only one PK
-- most likely this PK is an MPC of some type -- to do the actual work.
------------------------------------------------------------------------- */

#include <iostream>
#include <unistd.h>
#include <sys/resource.h>
#include "errors.hh"

#include "Teuchos_VerboseObjectParameterListHelpers.hpp"
#include "Teuchos_XMLParameterListHelpers.hpp"
#include "Teuchos_TimeMonitor.hpp"
#include "AmanziComm.hh"
#include "AmanziTypes.hh"

#include "InputAnalysis.hh"

#include "Units.hh"
#include "CompositeVector.hh"
#include "TimeStepManager.hh"
#include "Visualization.hh"
#include "VisualizationDomainSet.hh"
#include "IO.hh"
#include "Checkpoint.hh"
#include "UnstructuredObservations.hh"
#include "State.hh"
#include "PK.hh"
#include "TreeVector.hh"
#include "PK_Factory.hh"
#include "pk_helpers.hh"

#include "coordinator.hh"

#define DEBUG_MODE 1

namespace ATS {

Coordinator::Coordinator(Teuchos::ParameterList& parameter_list,
                         Teuchos::RCP<Amanzi::State>& S,
                         Amanzi::Comm_ptr_type comm ) :
    parameter_list_(Teuchos::rcp(new Teuchos::ParameterList(parameter_list))),
    S_(S),
    comm_(comm),
    restart_(false)
{
  // create and start the global timer
  timer_ = Teuchos::rcp(new Teuchos::Time("wallclock_monitor",true));
  setup_timer_ = Teuchos::TimeMonitor::getNewCounter("setup");
  cycle_timer_ = Teuchos::TimeMonitor::getNewCounter("cycle");
  coordinator_init();

  vo_ = Teuchos::rcp(new Amanzi::VerboseObject("Coordinator", *coordinator_list_));
};

void Coordinator::coordinator_init()
{
  coordinator_list_ = Teuchos::sublist(parameter_list_, "cycle driver");
  read_parameter_list();

  // create the top level PK
  Teuchos::RCP<Teuchos::ParameterList> pks_list = Teuchos::sublist(parameter_list_, "PKs");
  Teuchos::ParameterList pk_tree_list = coordinator_list_->sublist("PK tree");
  if (pk_tree_list.numParams() != 1) {
    Errors::Message message("CycleDriver: PK tree list should contain exactly one root node list");
    Exceptions::amanzi_throw(message);
  }
  Teuchos::ParameterList::ConstIterator pk_item = pk_tree_list.begin();
  const std::string &pk_name = pk_tree_list.name(pk_item);

  // create the solution
  soln_ = Teuchos::rcp(new Amanzi::TreeVector());

  // create the pk
  Amanzi::PKFactory pk_factory;
  pk_ = pk_factory.CreatePK(pk_name, pk_tree_list, parameter_list_, S_, soln_);

  // create the checkpointing
  Teuchos::ParameterList& chkp_plist = parameter_list_->sublist("checkpoint");
  checkpoint_ = Teuchos::rcp(new Amanzi::Checkpoint(chkp_plist, *S_));

  // create the observations
  Teuchos::ParameterList& observation_plist = parameter_list_->sublist("observations");
  for (auto& sublist : observation_plist) {
    if (observation_plist.isSublist(sublist.first)) {
      observations_.emplace_back(Teuchos::rcp(new Amanzi::UnstructuredObservations(
                observation_plist.sublist(sublist.first))));
    } else {
      Errors::Message msg("\"observations\" list must only include sublists.");
      Exceptions::amanzi_throw(msg);
    }
  }

  for (Amanzi::State::mesh_iterator mesh=S_->mesh_begin();
       mesh!=S_->mesh_end(); ++mesh) {

    if (S_->IsDeformableMesh(mesh->first) && !S_->IsAliasedMesh(mesh->first)) {
      Amanzi::Key node_key = Amanzi::Keys::getKey(mesh->first, "vertex_coordinates");
      S_->Require<Amanzi::CompositeVector,Amanzi::CompositeVectorSpace>(
        node_key, Amanzi::Tags::NEXT, node_key)
        .SetMesh(mesh->second.first)->SetGhosted()
        ->SetComponent("node", Amanzi::AmanziMesh::NODE, mesh->second.first->space_dimension());
    }

    // writes region information
    if (parameter_list_->isSublist("analysis")) {
      Amanzi::InputAnalysis analysis(mesh->second.first, mesh->first);
      analysis.Init(parameter_list_->sublist("analysis").sublist(mesh->first));
      analysis.RegionAnalysis();
      analysis.OutputBCs();
    }
  }

  // create the time step manager
  tsm_ = Teuchos::rcp(new Amanzi::TimeStepManager());
}

void Coordinator::setup()
{
  // common constants
  S_->Require<double>("atmospheric_pressure",
                      Amanzi::Tags::DEFAULT, "coordinator");
  S_->Require<Amanzi::AmanziGeometry::Point>("gravity",
          Amanzi::Tags::DEFAULT, "coordinator");

  // needed other times
  S_->require_time(Amanzi::Tags::CURRENT);
  S_->require_time(Amanzi::Tags::NEXT);

  // order matters here -- PKs set the leaves, then observations can use those
  // if provided, and setup finally deals with all secondaries and allocates memory
  pk_->set_tags(Amanzi::Tags::CURRENT, Amanzi::Tags::NEXT);
  pk_->Setup();
  for (auto& obs : observations_) obs->Setup(S_.ptr());
  S_->Setup();
}

void Coordinator::initialize()
{
  Teuchos::OSTab tab = vo_->getOSTab();
  int size = comm_->NumProc();
  int rank = comm_->MyPID();

  S_->set_time(Amanzi::Tags::CURRENT, t0_);
  S_->set_time(Amanzi::Tags::NEXT, t0_);
  S_->set_cycle(cycle0_);

  // Restart from checkpoint part 1:
  //  - get the time prior to initializing anything else
  if (restart_) {
    double t_restart = Amanzi::ReadCheckpointInitialTime(comm_, restart_filename_);
    S_->set_time(Amanzi::Tags::CURRENT, t_restart);
    S_->set_time(Amanzi::Tags::NEXT, t_restart);
    t0_ = t_restart;
  }

  // Initialize the state
  S_->InitializeFields();

  // Initialize the process kernels
  pk_->Initialize();

  // calling CommitStep to set up copies as needed
  pk_->CommitStep(t0_, t0_, Amanzi::Tags::NEXT);

  // initialize vertex coordinate data
  for (Amanzi::State::mesh_iterator mesh=S_->mesh_begin();
       mesh!=S_->mesh_end(); ++mesh) {
    if (S_->IsDeformableMesh(mesh->first) && !S_->IsAliasedMesh(mesh->first)) {
      Amanzi::Key node_key = Amanzi::Keys::getKey(mesh->first, "vertex_coordinates");
      copyMeshCoordinatesToVector(*mesh->second.first,
              S_->GetW<Amanzi::CompositeVector>(node_key, Amanzi::Tags::NEXT, node_key));
      S_->GetRecordW(node_key, Amanzi::Tags::NEXT, node_key).set_initialized();
    }
  }

  // Restart from checkpoint part 2:
  // -- load all other data
  if (restart_) {
    Amanzi::ReadCheckpoint(comm_, *S_, restart_filename_);
    t0_ = S_->get_time(Amanzi::Tags::DEFAULT);

    cycle0_ = S_->Get<int>("cycle", Amanzi::Tags::DEFAULT);
    S_->set_time(Amanzi::Tags::CURRENT, t0_);
    S_->set_time(Amanzi::Tags::NEXT, t0_);

    for (Amanzi::State::mesh_iterator mesh=S_->mesh_begin();
         mesh!=S_->mesh_end(); ++mesh) {
      if (S_->IsDeformableMesh(mesh->first)) {
        Amanzi::DeformCheckpointMesh(*S_, mesh->first);
      }
    }
  }

  // Final checks.
  //S_->CheckNotEvaluatedFieldsInitialized();
  S_->InitializeEvaluators();
  S_->InitializeFieldCopies();
  S_->CheckAllFieldsInitialized();

  // commit the initial conditions.
  pk_->CommitStep(S_->get_time(), S_->get_time(), Amanzi::Tags::NEXT);

  // Write dependency graph.
  S_->WriteDependencyGraph();
  S_->InitializeIOFlags();

  // Check final initialization
  WriteStateStatistics(*S_, *vo_);

  // Set up visualization
  auto vis_list = Teuchos::sublist(parameter_list_,"visualization");
  for (auto& entry : *vis_list) {
    std::string domain_name = entry.first;

    if (S_->HasMesh(domain_name)) {
      // visualize standard domain
      auto mesh_p = S_->GetMesh(domain_name);
      auto sublist_p = Teuchos::sublist(vis_list, domain_name);
      if (!sublist_p->isParameter("file name base")) {
        if (domain_name.empty() || domain_name == "domain") {
          sublist_p->set<std::string>("file name base", std::string("ats_vis"));
        } else {
          sublist_p->set<std::string>("file name base", std::string("ats_vis_")+domain_name);
        }
      }

      if (S_->HasMesh(domain_name+"_3d") && sublist_p->get<bool>("visualize on 3D mesh", true))
        mesh_p = S_->GetMesh(domain_name+"_3d");

      // vis successful timesteps
      auto vis = Teuchos::rcp(new Amanzi::Visualization(*sublist_p));
      vis->set_name(domain_name);
      vis->set_mesh(mesh_p);
      vis->CreateFiles(false);
      visualization_.push_back(vis);

    } else if (Amanzi::Keys::isDomainSet(domain_name)) {
      // visualize domain set
      const auto& dset = S_->GetDomainSet(Amanzi::Keys::getDomainSetName(domain_name));
      auto sublist_p = Teuchos::sublist(vis_list, domain_name);

      if (sublist_p->get("visualize individually", false)) {
        // visualize each subdomain
        for (const auto& subdomain : *dset) {
          Teuchos::ParameterList sublist = vis_list->sublist(subdomain);
          sublist.set<std::string>("file name base", std::string("ats_vis_")+subdomain);
          auto vis = Teuchos::rcp(new Amanzi::Visualization(sublist));
          vis->set_name(subdomain);
          vis->set_mesh(S_->GetMesh(subdomain));
          vis->CreateFiles(false);
          visualization_.push_back(vis);
        }
      } else {
        // visualize collectively
        auto domain_name_base = Amanzi::Keys::getDomainSetName(domain_name);
        if (!sublist_p->isParameter("file name base"))
          sublist_p->set("file name base", std::string("ats_vis_")+domain_name_base);
        auto vis = Teuchos::rcp(new Amanzi::VisualizationDomainSet(*sublist_p));
        vis->set_name(domain_name_base);
        vis->set_domain_set(dset);
        vis->set_mesh(dset->get_referencing_parent());
        vis->CreateFiles(false);
        visualization_.push_back(vis);
      }
    }
  }

  // make observations at time 0
  for (const auto& obs : observations_) obs->MakeObservations(S_.ptr());

  // set up the TSM
  // -- register visualization times
  for (const auto& vis : visualization_) vis->RegisterWithTimeStepManager(tsm_.ptr());

  // -- register checkpoint times
  checkpoint_->RegisterWithTimeStepManager(tsm_.ptr());

  // -- register observation times
  for (const auto& obs : observations_) obs->RegisterWithTimeStepManager(tsm_.ptr());

  // -- register the final time
  tsm_->RegisterTimeEvent(t1_);

  // -- register any intermediate requested times
  if (coordinator_list_->isSublist("required times")) {
    Teuchos::ParameterList& sublist = coordinator_list_->sublist("required times");
    Amanzi::IOEvent pause_times(sublist);
    pause_times.RegisterWithTimeStepManager(tsm_.ptr());
  }

  // -- advance cycle to 0 and begin
  if (S_->get_cycle() == -1) S_->advance_cycle();
}


void Coordinator::finalize()
{
  // Force checkpoint at the end of simulation, and copy to checkpoint_final
  pk_->CalculateDiagnostics(Amanzi::Tags::NEXT);
  WriteCheckpoint(*checkpoint_, comm_, *S_, true);

  // flush observations to make sure they are saved
  for (const auto& obs : observations_) obs->Flush();
}


double
rss_usage()
{ // return ru_maxrss in MBytes
#if (defined(__unix__) || defined(__unix) || defined(unix) || defined(__APPLE__) || defined(__MACH__))
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
#if (defined(__APPLE__) || defined(__MACH__))
  return static_cast<double>(usage.ru_maxrss)/1024.0/1024.0;
#else
  return static_cast<double>(usage.ru_maxrss)/1024.0;
#endif
#else
  return 0.0;
#endif
}


void
Coordinator::report_memory()
{
  // report the memory high water mark (using ru_maxrss)
  // this should be called at the very end of a simulation
  if (vo_->os_OK(Teuchos::VERB_MEDIUM)) {
    double global_ncells(0.0);
    double local_ncells(0.0);
    for (Amanzi::State::mesh_iterator mesh = S_->mesh_begin(); mesh != S_->mesh_end(); ++mesh) {
      Epetra_Map cell_map = (mesh->second.first)->cell_map(false);
      global_ncells += cell_map.NumGlobalElements();
      local_ncells += cell_map.NumMyElements();
    }

    double mem = rss_usage();

    double percell(mem);
    if (local_ncells > 0) {
      percell = mem/local_ncells;
    }

    double max_percell(0.0);
    double min_percell(0.0);
    comm_->MinAll(&percell,&min_percell,1);
    comm_->MaxAll(&percell,&max_percell,1);

    double total_mem(0.0);
    double max_mem(0.0);
    double min_mem(0.0);
    comm_->SumAll(&mem,&total_mem,1);
    comm_->MinAll(&mem,&min_mem,1);
    comm_->MaxAll(&mem,&max_mem,1);

    Teuchos::OSTab tab = vo_->getOSTab();
    *vo_->os() << "======================================================================" << std::endl;
    *vo_->os() << "All meshes combined have " << global_ncells << " cells." << std::endl;
    *vo_->os() << "Memory usage (high water mark):" << std::endl;
    *vo_->os() << std::fixed << std::setprecision(1);
    *vo_->os() << "  Maximum per core:   " << std::setw(7) << max_mem
          << " MBytes,  maximum per cell: " << std::setw(7) << max_percell*1024*1024
          << " Bytes" << std::endl;
    *vo_->os() << "  Minimum per core:   " << std::setw(7) << min_mem
          << " MBytes,  minimum per cell: " << std::setw(7) << min_percell*1024*1024
         << " Bytes" << std::endl;
    *vo_->os() << "  Total:              " << std::setw(7) << total_mem
          << " MBytes,  total per cell:   " << std::setw(7) << total_mem/global_ncells*1024*1024
          << " Bytes" << std::endl;
  }


  // double doubles_count(0.0);
  // for (Amanzi::State::data_iterator field=S_->data_begin(); field!=S_->data_end(); ++field) {
  //   doubles_count += static_cast<double>(field->second->GetLocalElementCount());
  // }
  // double global_doubles_count(0.0);
  // double min_doubles_count(0.0);
  // double max_doubles_count(0.0);
  // comm_->SumAll(&doubles_count,&global_doubles_count,1);
  // comm_->MinAll(&doubles_count,&min_doubles_count,1);
  // comm_->MaxAll(&doubles_count,&max_doubles_count,1);

  // Teuchos::OSTab tab = vo_->getOSTab();
  // *vo_->os() << "Doubles allocated in state fields " << std::endl;
  // *vo_->os() << "  Maximum per core:   " << std::setw(7)
  //            << max_doubles_count*8/1024/1024 << " MBytes" << std::endl;
  // *vo_->os() << "  Minimum per core:   " << std::setw(7)
  //            << min_doubles_count*8/1024/1024 << " MBytes" << std::endl;
  // *vo_->os() << "  Total:              " << std::setw(7)
  //            << global_doubles_count*8/1024/1024 << " MBytes" << std::endl;
}



void
Coordinator::read_parameter_list()
{
  Amanzi::Utils::Units units;
  t0_ = coordinator_list_->get<double>("start time");
  std::string t0_units = coordinator_list_->get<std::string>("start time units", "s");
  if (!units.IsValidTime(t0_units)) {
    Errors::Message msg;
    msg << "Coordinator start time: unknown time units type: \"" << t0_units << "\"  Valid are: " << units.ValidTimeStrings();
    Exceptions::amanzi_throw(msg);
  }
  bool success;
  t0_ = units.ConvertTime(t0_, t0_units, "s", success);

  t1_ = coordinator_list_->get<double>("end time");
  std::string t1_units = coordinator_list_->get<std::string>("end time units", "s");
  if (!units.IsValidTime(t1_units)) {
    Errors::Message msg;
    msg << "Coordinator end time: unknown time units type: \"" << t1_units << "\"  Valid are: " << units.ValidTimeStrings();
    Exceptions::amanzi_throw(msg);
  }
  t1_ = units.ConvertTime(t1_, t1_units, "s", success);

  max_dt_ = coordinator_list_->get<double>("max time step size [s]", 1.0e99);
  min_dt_ = coordinator_list_->get<double>("min time step size [s]", 1.0e-12);
  cycle0_ = coordinator_list_->get<int>("start cycle",-1);
  cycle1_ = coordinator_list_->get<int>("end cycle",-1);
  duration_ = coordinator_list_->get<double>("wallclock duration [hrs]", -1.0);
  subcycled_ts_ = coordinator_list_->get<bool>("subcycled timestep", false);

  // restart control
  restart_ = coordinator_list_->isParameter("restart from checkpoint file");
  if (restart_) restart_filename_ = coordinator_list_->get<std::string>("restart from checkpoint file");
}



// -----------------------------------------------------------------------------
// acquire the chosen timestep size
// -----------------------------------------------------------------------------
double
Coordinator::get_dt(bool after_fail)
{
  // get the physical step size
  double dt = pk_->get_dt();
  double dt_pk = dt;
  if (dt < 0.) return dt;

  // check if the step size has gotten too small
  if (dt < min_dt_) {
    Errors::Message message("Coordinator: error, timestep too small");
    Exceptions::amanzi_throw(message);
  }

  // cap the max step size
  if (dt > max_dt_) {
    dt = max_dt_;
  }

  // ask the step manager if this step is ok
  dt = tsm_->TimeStep(S_->get_time(Amanzi::Tags::NEXT), dt, after_fail);
  if (subcycled_ts_) dt = std::min(dt, dt_pk);
  return dt;
}


bool
Coordinator::advance()
{
  double dt = S_->Get<double>("dt", Amanzi::Tags::DEFAULT);
  double t_old = S_->get_time(Amanzi::Tags::CURRENT);
  double t_new = S_->get_time(Amanzi::Tags::NEXT);

  bool fail = pk_->AdvanceStep(t_old, t_new, false);
  if (!fail) fail |= !pk_->ValidStep();

  // write state post-advance, if extreme
  WriteStateStatistics(*S_, *vo_, Teuchos::VERB_EXTREME);

  if (!fail) {
    // commit the state, copying NEXT --> CURRENT
    pk_->CommitStep(t_old, t_new, Amanzi::Tags::NEXT);

  } else {
    // Failed the timestep.
    // Potentially write out failed timestep for debugging
    for (const auto& vis : failed_visualization_) WriteVis(*vis, *S_);

    // copy from old time into new time to reset the timestep
    pk_->FailStep(t_old, t_new, Amanzi::Tags::NEXT);

    // check whether meshes are deformable, and if so, recover the old coordinates
    for (Amanzi::State::mesh_iterator mesh=S_->mesh_begin();
         mesh!=S_->mesh_end(); ++mesh) {
      if (S_->IsDeformableMesh(mesh->first) && !S_->IsAliasedMesh(mesh->first)) {
        // collect the old coordinates
        Amanzi::Key node_key = Amanzi::Keys::getKey(mesh->first, "vertex_cooordinates");
        Teuchos::RCP<const Amanzi::CompositeVector> vc_vec =
          S_->GetPtr<Amanzi::CompositeVector>(node_key, Amanzi::Tags::DEFAULT);
        vc_vec->ScatterMasterToGhosted();
        const Epetra_MultiVector& vc = *vc_vec->ViewComponent("node", true);

        std::vector<int> node_ids(vc.MyLength());
        Amanzi::AmanziGeometry::Point_List old_positions(vc.MyLength());
        for (int n=0; n!=vc.MyLength(); ++n) {
          node_ids[n] = n;
          if (mesh->second.first->space_dimension() == 2) {
            old_positions[n] = Amanzi::AmanziGeometry::Point(vc[0][n], vc[1][n]);
          } else {
            old_positions[n] = Amanzi::AmanziGeometry::Point(vc[0][n], vc[1][n], vc[2][n]);
          }
        }

        // undeform the mesh
        Amanzi::AmanziGeometry::Point_List final_positions;
        mesh->second.first->deform(node_ids, old_positions, false, &final_positions);
      }
    }
  }
  // write state one extreme, post-commit/fail
  WriteStateStatistics(*S_, *vo_, Teuchos::VERB_EXTREME);

  return fail;
}


void Coordinator::visualize(bool force)
{
  // write visualization if requested
  bool dump = force;
  int cycle = S_->get_cycle();
  double time = S_->get_time();

  if (!dump) {
    for (const auto& vis : visualization_) {
      dump |= vis->DumpRequested(cycle, time);
    }
  }

  if (dump) {
    pk_->CalculateDiagnostics(Amanzi::Tags::NEXT);
  }

  for (const auto& vis : visualization_) {
    if (force || vis->DumpRequested(cycle, time)) {
      WriteVis(*vis, *S_);
    }
  }
}


void Coordinator::checkpoint(bool force)
{
  int cycle = S_->get_cycle();
  double time = S_->get_time();
  if (force || checkpoint_->DumpRequested(cycle, time)) {
    WriteCheckpoint(*checkpoint_, comm_, *S_);
  }
}


// -----------------------------------------------------------------------------
// timestep loop
// -----------------------------------------------------------------------------
void Coordinator::cycle_driver() {
  // wallclock duration -- in seconds
  const double duration(duration_ * 3600);

  // start at time t = t0 and initialize the state.
  {
    Teuchos::TimeMonitor monitor(*setup_timer_);
    setup();
    initialize();
  }

  // get the intial timestep
  double dt = get_dt(false);
  if (!restart_) {
    S_->Assign<double>("dt", Amanzi::Tags::DEFAULT, "dt", dt);
  }

  // visualization at IC
  visualize();
  checkpoint();

  // iterate process kernels
  //
  // Make sure times are set up correctly
  AMANZI_ASSERT(std::abs(S_->get_time(Amanzi::Tags::NEXT)
                         - S_->get_time(Amanzi::Tags::CURRENT)) < 1.e-4);
  {
    Teuchos::TimeMonitor cycle_monitor(*cycle_timer_);
    double dt = S_->Get<double>("dt", Amanzi::Tags::DEFAULT);
#if !DEBUG_MODE
  try {
#endif

    while (((t1_ < 0) || (S_->get_time() < t1_)) &&
           ((cycle1_ == -1) || (S_->get_cycle() <= cycle1_)) &&
           ((duration_ < 0) || (timer_->totalElapsedTime(true) < duration)) &&
           (dt > 0.)) {
      if (vo_->os_OK(Teuchos::VERB_LOW)) {
        Teuchos::OSTab tab = vo_->getOSTab();
        *vo_->os() << "======================================================================"
                  << std::endl << std::endl;
        *vo_->os() << "Cycle = " << S_->get_cycle();
        *vo_->os() << ",  Time [days] = "<< std::setprecision(16) << S_->get_time() / (60*60*24);
        *vo_->os() << ",  dt [days] = " << std::setprecision(16) << dt / (60*60*24)  << std::endl;
        *vo_->os() << "----------------------------------------------------------------------"
                  << std::endl;
      }

      S_->Assign<double>("dt", Amanzi::Tags::DEFAULT, "dt", dt);
      S_->advance_time(Amanzi::Tags::NEXT, dt);
      bool fail = advance();

      if (fail) {
        // reset t_new
        S_->set_time(Amanzi::Tags::NEXT, S_->get_time(Amanzi::Tags::CURRENT));
      } else {
        S_->set_time(Amanzi::Tags::CURRENT, S_->get_time(Amanzi::Tags::NEXT));
        S_->advance_cycle();

        // make observations, vis, and checkpoints
        for (const auto& obs : observations_) obs->MakeObservations(S_.ptr());
        visualize();
        checkpoint(); // checkpoint with the new dt
      }

      dt = get_dt(fail);
    } // while not finished

#if !DEBUG_MODE
  } catch (Amanzi::Exceptions::Amanzi_exception &e) {
    // write one more vis for help debugging
    S_->advance_cycle(Amanzi::Tags::NEXT);
    visualize(true); // force vis

    // flush observations to make sure they are saved
    for (const auto& obs : observations_) obs->Flush();

    // catch errors to dump two checkpoints -- one as a "last good" checkpoint
    // and one as a "debugging data" checkpoint.
    checkpoint_->set_filebasename("last_good_checkpoint");
    WriteCheckpoint(checkpoint_.ptr(), comm_, *S_);
    checkpoint_->set_filebasename("error_checkpoint");
    WriteCheckpoint(checkpoint_.ptr(), comm_, *S_);
    throw e;
  }
#endif
  }

  // finalizing simulation
  WriteStateStatistics(*S_, *vo_);
  report_memory();
  Teuchos::TimeMonitor::summarize(*vo_->os());

  finalize();
} // cycle driver


} // close namespace Amanzi
