/*
This file is part of Vlasiator.

Copyright 2010, 2011, 2012 Finnish Meteorological Institute

Vlasiator is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License version 3
as published by the Free Software Foundation.

Vlasiator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdlib>
#include <iostream>
#include <cmath>
#include <vector>
#include <sstream>
#include <ctime>

#include "vlasovmover.h"
#include "definitions.h"
#include "mpiconversion.h"
#include "logger.h"
#include "parameters.h"
#include "readparameters.h"
#include "spatial_cell.hpp"
#include "datareduction/datareducer.h"
#include "sysboundary/sysboundary.h"
#include "transferstencil.h"

#include "vlsvwriter2.h" 
#include "fieldsolver.h"
#include "projects/project.h"
#include "grid.h"
#include "iowrite.h"
#include "ioread.h"

#ifdef CATCH_FPE
#include <fenv.h>
#include <signal.h>
/*! Function used to abort the program upon detecting a floating point exception. Which exceptions are caught is defined using the function feenableexcept.
 */
void fpehandler(int sig_num)
{
   signal(SIGFPE, fpehandler);
   printf("SIGFPE: floating point exception occured, exiting.\n");
   abort();
}
#endif

#include "phiprof.hpp"

Logger logFile, diagnostic;

using namespace std;
using namespace phiprof;

void addTimedBarrier(string name){
#ifdef NDEBUG
//let's not do  a barrier
   return; 
#endif
   int bt=phiprof::initializeTimer(name,"Barriers","MPI");
   phiprof::start(bt);
   MPI_Barrier(MPI_COMM_WORLD);
   phiprof::stop(bt);
}


bool computeNewTimeStep(dccrg::Dccrg<SpatialCell>& mpiGrid,Real &newDt, bool &isChanged) {

   phiprof::start("compute-timestep");
   //compute maximum time-step, this cannot be done at the first
   //step as the solvers compute the limits for each cell

   isChanged=false;

   vector<uint64_t> cells = mpiGrid.get_cells();
   /* Arrays for storing local (per process) and global max dt
      0th position stores ordinary space propagation dt
      1st position stores velocity space propagation dt
      2nd position stores field propagation dt
   */
   Real dtMaxLocal[3];
   Real dtMaxGlobal[3];
  
   dtMaxLocal[0]=std::numeric_limits<Real>::max();
   dtMaxLocal[1]=std::numeric_limits<Real>::max();
   dtMaxLocal[2]=std::numeric_limits<Real>::max();
   for (std::vector<uint64_t>::const_iterator cell_id = cells.begin(); cell_id != cells.end(); ++cell_id) {
      SpatialCell* cell = mpiGrid[*cell_id];
      if ( cell->sysBoundaryFlag == sysboundarytype::NOT_SYSBOUNDARY ||
           (cell->sysBoundaryLayer == 1 && cell->sysBoundaryFlag != sysboundarytype::NOT_SYSBOUNDARY )) {
	//spatial fluxes computed also for boundary cells              
	dtMaxLocal[0]=min(dtMaxLocal[0], cell->parameters[CellParams::MAXRDT]);
	dtMaxLocal[2]=min(dtMaxLocal[2], cell->parameters[CellParams::MAXFDT]);
      }
      
      if (cell->sysBoundaryFlag == sysboundarytype::NOT_SYSBOUNDARY) {
	//Acceleration only done on non sysboundary cells
	dtMaxLocal[1]=min(dtMaxLocal[1], cell->parameters[CellParams::MAXVDT]);
      }
   }
   MPI_Allreduce(&(dtMaxLocal[0]), &(dtMaxGlobal[0]), 3, MPI_Type<Real>(), MPI_MIN, MPI_COMM_WORLD);
   
   
   Real maxVDtNoSubstepping=dtMaxGlobal[1];
   
   //Increase timestep limit in velocity space based on
   //maximum number of substeps we are allowed to take. As
   //the length of each substep is unknown beforehand the
   //limit is not hard, it may be exceeded in some cases.
   // A value of 0 means that there is no limit on substepping
   if(P::maxAccelerationSubsteps==0)
      dtMaxGlobal[1]=std::numeric_limits<Real>::max();
   else
      dtMaxGlobal[1]*=P::maxAccelerationSubsteps;
   
   //If fieldsolver is off there should be no limit on time-step from it
   if (P::propagateField == false) {
      dtMaxGlobal[2]=std::numeric_limits<Real>::max();
   }
   
   //If vlasovsolver is off there should be no limit on time-step from it
   if (P::propagateVlasov == false) {
      dtMaxGlobal[0]=std::numeric_limits<Real>::max();
      dtMaxGlobal[1]=std::numeric_limits<Real>::max();
      maxVDtNoSubstepping=std::numeric_limits<Real>::max();
   }
   
   //reduce dt if it is too high for any of the three propagators, or too low for all propagators
   if(( P::dt > dtMaxGlobal[0]*P::vlasovSolverMaxCFL ||
        P::dt > dtMaxGlobal[1]*P::vlasovSolverMaxCFL ||
        P::dt > dtMaxGlobal[2]*P::fieldSolverMaxCFL ) ||
      ( P::dt < dtMaxGlobal[0]*P::vlasovSolverMinCFL && 
        P::dt < dtMaxGlobal[1]*P::vlasovSolverMinCFL &&
	P::dt < dtMaxGlobal[2]*P::fieldSolverMinCFL )
      ) {
     //new dt computed
     isChanged=true;

     //set new timestep to the lowest one of all interval-midpoints
     newDt = 0.5*(P::vlasovSolverMaxCFL+ P::vlasovSolverMinCFL)*dtMaxGlobal[0];
     newDt = min(newDt,0.5*(P::vlasovSolverMaxCFL+ P::vlasovSolverMinCFL)*dtMaxGlobal[1]);
     newDt = min(newDt,0.5*(P::fieldSolverMaxCFL+ P::fieldSolverMinCFL)*dtMaxGlobal[2]);
   
     logFile <<"(TIMESTEP) New dt = " << newDt << " computed on step "<<  P::tstep <<" at " <<P::t << 
       "s   Maximum possible dt (not including  vlasovsolver CFL "<< 
       P::vlasovSolverMinCFL <<"-"<<P::vlasovSolverMaxCFL<<
       " or fieldsolver CFL "<< 
       P::fieldSolverMinCFL <<"-"<<P::fieldSolverMaxCFL<<
       " ) in {r, v+subs, v, BE} was " <<
       dtMaxGlobal[0] << " " <<
       dtMaxGlobal[1] << " " <<
       maxVDtNoSubstepping << " " <<
       dtMaxGlobal[2] << endl << writeVerbose;
   }
	

   phiprof::stop("compute-timestep");

   return true;
}



int main(int argn,char* args[]) {
   bool success = true;
   int myRank;
   const creal DT_EPSILON=1e-12;
   typedef Parameters P;
   Real newDt;
   bool dtIsChanged;


// Init MPI: 
   int required=MPI_THREAD_FUNNELED;
   int provided;
   MPI_Init_thread(&argn,&args,required,&provided);
   if (required > provided){
      MPI_Comm_rank(MPI_COMM_WORLD,&myRank);
      if(myRank==MASTER_RANK)
         cerr << "(MAIN): MPI_Init_thread failed! Got " << provided << ", need "<<required <<endl;
      exit(1);
   }    
   
   double initialWtime =  MPI_Wtime();
   
   MPI_Comm comm = MPI_COMM_WORLD;
   MPI_Comm_rank(comm,&myRank);
   dccrg::Dccrg<SpatialCell> mpiGrid;
   SysBoundary sysBoundaries;
   bool isSysBoundaryCondDynamic;
   
   #ifdef CATCH_FPE
   // WARNING FE_INEXACT is too sensitive to be used. See man fenv.
   //feenableexcept(FE_DIVBYZERO|FE_INVALID|FE_OVERFLOW|FE_UNDERFLOW);
   feenableexcept(FE_DIVBYZERO|FE_INVALID|FE_OVERFLOW);
   //feenableexcept(FE_DIVBYZERO|FE_INVALID);
   signal(SIGFPE, fpehandler);
   #endif
   
   phiprof::start("main");
   phiprof::start("Initialization");
   phiprof::start("Read parameters");
   //init parameter file reader
   Readparameters readparameters(argn,args,MPI_COMM_WORLD);
   P::addParameters();
   projects::Project::addParameters();
   sysBoundaries.addParameters();
   readparameters.parse();
   P::getParameters();
   
   Project* project = projects::createProject();
   
   project->getParameters();
   sysBoundaries.getParameters();
   phiprof::stop("Read parameters");
   
   phiprof::start("Init project");
   if (project->initialize() == false) {
      if(myRank == MASTER_RANK) cerr << "(MAIN): Project did not initialize correctly!" << endl;
      exit(1);
   }
   phiprof::stop("Init project");

   // Init parallel logger:
   phiprof::start("open logFile & diagnostic");
   //if restarting we will append to logfiles
   if (logFile.open(MPI_COMM_WORLD,MASTER_RANK,"logfile.txt",P::isRestart) == false) {
      if(myRank == MASTER_RANK) cerr << "(MAIN) ERROR: Logger failed to open logfile!" << endl;
      exit(1);
   }
   if (P::diagnosticInterval != 0) {
      if (diagnostic.open(MPI_COMM_WORLD,MASTER_RANK,"diagnostic.txt",P::isRestart) == false) {
         if(myRank == MASTER_RANK) cerr << "(MAIN) ERROR: Logger failed to open diagnostic file!" << endl;
         exit(1);
      }
   }
   phiprof::stop("open logFile & diagnostic");
   
   phiprof::start("Init grid");
   initializeGrid(
      argn,
      args,
      mpiGrid,
      sysBoundaries,
      *project
   );
   isSysBoundaryCondDynamic = sysBoundaries.isDynamic();
   phiprof::stop("Init grid");
   
   phiprof::start("Init DROs");
   // Initialize data reduction operators. This should be done elsewhere in order to initialize 
   // user-defined operators:
   DataReducer outputReducer, diagnosticReducer;
   initializeDataReducers(&outputReducer, &diagnosticReducer);
   phiprof::stop("Init DROs");
   
   //VlsWriter vlsWriter;
   phiprof::start("Init vlasov propagator");
   // Initialize Vlasov propagator:
   if (initializeMover(mpiGrid) == false) {
      logFile << "(MAIN): Vlasov propagator did not initialize correctly!" << endl << writeVerbose;
      exit(1);
   }
   //compute moments, and set them  in RHO*. If restart, they are already read in
   if(!P::isRestart) {
      calculateVelocityMoments(mpiGrid);
   }
   phiprof::stop("Init vlasov propagator");
   
   phiprof::start("Init field propagator");
   // Initialize field propagator:
   if (initializeFieldPropagator(mpiGrid, sysBoundaries) == false) {
       logFile << "(MAIN): Field propagator did not initialize correctly!" << endl << writeVerbose;
       exit(1);
   }
   phiprof::stop("Init field propagator");
   
   // Free up memory:
   readparameters.finalize();

         
   bool updateVelocityBlocksAfterAcceleration=false;
#ifdef SEMILAG
   updateVelocityBlocksAfterAcceleration=true;
#endif
   if(P::maxAccelerationSubsteps!=1)
      updateVelocityBlocksAfterAcceleration=true;


   // Save restart data
   if (P::writeInitialState) {
      phiprof::start("write-initial-state");
      if (myRank == MASTER_RANK)
         logFile << "(IO): Writing initial state to disk, tstep = "  << endl << writeVerbose;
//    P::systemWriteDistributionWriteStride[i], P::systemWriteName[i], P::systemWrites[i]
      P::systemWriteDistributionWriteStride.push_back(1);
      P::systemWriteName.push_back("initial-grid");
      P::systemWriteDistributionWriteXlineStride.push_back(0);
      P::systemWriteDistributionWriteYlineStride.push_back(0);
      P::systemWriteDistributionWriteZlineStride.push_back(0);
      
      for(uint si=0; si<P::systemWriteName.size(); si++) {
         P::systemWrites.push_back(0);
      }
      
      writeGrid(mpiGrid,outputReducer,P::systemWriteName.size()-1);
      
      P::systemWriteDistributionWriteStride.pop_back();
      P::systemWriteName.pop_back();
      P::systemWriteDistributionWriteXlineStride.pop_back();
      P::systemWriteDistributionWriteYlineStride.pop_back();
      P::systemWriteDistributionWriteZlineStride.pop_back();
      
      phiprof::stop("write-initial-state");
   }
   
         

   if(P::dynamicTimestep && !P::isRestart) {
      //compute vlasovsolver once with zero dt, this is  to initialize
      //per-cell dt limits. In restarts, we read in dt from file
      phiprof::start("compute-dt");
      if(P::propagateVlasov) {
         //Flux computation is sufficient, no need to propagate
         calculateSpatialFluxes(mpiGrid, sysBoundaries, 0.0);
         calculateAcceleration(mpiGrid,0.0);
      }
      if(updateVelocityBlocksAfterAcceleration){
         //this is probably not ever needed, as a zero length step
         //should not require changes
         updateRemoteVelocityBlockLists(mpiGrid);
         adjustVelocityBlocks(mpiGrid);
      }
      if(P::propagateField) {
         propagateFields(mpiGrid, sysBoundaries, 0.0);
      }
      //compute new dt
      computeNewTimeStep(mpiGrid,newDt,dtIsChanged);
      if(dtIsChanged)
         P::dt=newDt;
      phiprof::stop("compute-dt");

      if(P::maxAccelerationSubsteps!=1){
         //Now we make a small "hack" and compute an artifical number
         //of substeps, this is here to improve the initial load
         //balance, otherwise the first step may have a really bad load imbalance
         //get local cells         
         vector<uint64_t> cells = mpiGrid.get_cells();      
         
         for(uint i=0;i<cells.size();i++){
	    Real velocityDt=mpiGrid[cells[i]]->parameters[CellParams::MAXVDT]*0.5*(P::vlasovSolverMinCFL+P::vlasovSolverMaxCFL);

            if(velocityDt>0)
               mpiGrid[cells[i]]->subStepsAcceleration=(int)(P::dt/velocityDt)+1;
            else
               mpiGrid[cells[i]]->subStepsAcceleration=0;
               
         }
         //and balance load
         balanceLoad(mpiGrid);
      }
   }
   

   
   if(P::propagateVlasov && !P::isRestart) {
      //go forward by dt/2 in x, initializes leapfrog split. In restarts the
      //the distribution function is already propagated forward in time by dt/2
      phiprof::start("propagate-spatial-space-dt/2");
      calculateSpatialFluxes(mpiGrid, sysBoundaries, 0.5*P::dt);
      calculateSpatialPropagation(mpiGrid);
      phiprof::stop("propagate-spatial-space-dt/2");
   }

   
   
   phiprof::stop("Initialization");


   // ***********************************
   // ***** INITIALIZATION COMPLETE *****
   // ***********************************
   
   // Main simulation loop:
   if (myRank == MASTER_RANK) logFile << "(MAIN): Starting main simulation loop." << endl << writeVerbose;
   
   unsigned int computedCellsWithSubsteps=0;
   unsigned int computedCells=0;
   unsigned int computedTotalCells=0;
  //Compute here based on time what the file intervals are
   P::systemWrites.clear();
   for(uint i=0;i< P::systemWriteTimeInterval.size();i++){
       P::systemWrites.push_back((int)(P::t_min/P::systemWriteTimeInterval[i]));
   }
   
   unsigned int wallTimeRestartCounter=1;
   
   addTimedBarrier("barrier-end-initialization");
   
   phiprof::start("Simulation");
   double startTime=  MPI_Wtime();
   double beforeTime = MPI_Wtime();
   double beforeSimulationTime=P::t_min;
   double beforeStep=P::tstep_min;
   
   while(P::tstep <=P::tstep_max  &&
         P::t-P::dt <= P::t_max+DT_EPSILON &&
         wallTimeRestartCounter <= P::exitAfterRestarts) {

      addTimedBarrier("barrier-loop-start");
         
      phiprof::start("IO");
      //write out phiprof profiles and logs with a lower interval than normal
      //diagnostic (every 10 diagnostic intervals).
      logFile << "------------------ tstep = " << P::tstep << " t = " << P::t <<" dt = " << P::dt << " ------------------" << endl;
      if (P::diagnosticInterval != 0 &&
          P::tstep % (P::diagnosticInterval*10) == 0 &&
          P::tstep-P::tstep_min >0) {
         phiprof::print(MPI_COMM_WORLD,"phiprof_reduced",0.01);
         phiprof::print(MPI_COMM_WORLD,"phiprof_full");
         phiprof::printLogProfile(MPI_COMM_WORLD,P::tstep,"phiprof_log"," ",7);
         
         double currentTime=MPI_Wtime();
         double timePerStep=double(currentTime  - beforeTime) / (P::tstep-beforeStep);
         double timePerSecond=double(currentTime  - beforeTime) / (P::t-beforeSimulationTime + DT_EPSILON);
         double remainingTime=min(timePerStep*(P::tstep_max-P::tstep),timePerSecond*(P::t_max-P::t));
         time_t finalWallTime=time(NULL)+(time_t)remainingTime; //assume time_t is in seconds, as it is almost always
         struct tm *finalWallTimeInfo=localtime(&finalWallTime);
         logFile << "(TIME) current walltime/step " << timePerStep<< " s" <<endl;
         logFile << "(TIME) current walltime/simusecond " << timePerSecond<<" s" <<endl;
         logFile << "(TIME) Estimated completion time is " <<asctime(finalWallTimeInfo)<<endl;
         //reset before values, we want to report speed since last report of speed.
         beforeTime = MPI_Wtime();
         beforeSimulationTime=P::t;
         beforeStep=P::tstep;
      }               
      logFile << writeVerbose;
   

      // Check whether diagnostic output has to be produced
      if (P::diagnosticInterval != 0 && P::tstep % P::diagnosticInterval == 0) {
         phiprof::start("Diagnostic");
         if (writeDiagnostic(mpiGrid, diagnosticReducer) == false) {
            if(myRank == MASTER_RANK)  cerr << "ERROR with diagnostic computation" << endl;
            
         }
         phiprof::stop("Diagnostic");
      }
      // write system, loop through write classes
      for (uint i = 0; i < P::systemWriteTimeInterval.size(); i++) {
         if (P::systemWriteTimeInterval[i] >= 0.0 &&
                 P::t >= P::systemWrites[i] * P::systemWriteTimeInterval[i] - DT_EPSILON) {
            
            phiprof::start("write-system");
            logFile << "(IO): Writing spatial cell and reduced system data to disk, tstep = " << P::tstep << " t = " << P::t << endl << writeVerbose;
            writeGrid(mpiGrid, outputReducer, i);
            P::systemWrites[i]++;
            logFile << "(IO): .... done!" << endl << writeVerbose;
            phiprof::stop("write-system");
         }
      }
      
      
      // Write restart data if needed (based on walltime)
      int writeRestartWTime;
      if (myRank == MASTER_RANK) { 
         if (P::saveRestartWalltimeInterval >=0.0 && 
            P::saveRestartWalltimeInterval*wallTimeRestartCounter <=  MPI_Wtime()-initialWtime){
            writeRestartWTime = 1;
         }
         else {
            writeRestartWTime = 0;
         }  
      }
      MPI_Bcast( &writeRestartWTime, 1 , MPI_INT , MASTER_RANK ,MPI_COMM_WORLD);
            
      if (writeRestartWTime == 1){   
         phiprof::start("write-restart");
         wallTimeRestartCounter++;
        
         if (myRank == MASTER_RANK)
            logFile << "(IO): Writing spatial cell and restart data to disk, tstep = " << P::tstep << " t = " << P::t << endl << writeVerbose;
         writeRestart(mpiGrid,outputReducer,"restart",(uint)P::t);
         if (myRank == MASTER_RANK)
            logFile << "(IO): .... done!"<< endl << writeVerbose;
            
         phiprof::stop("write-restart");
         
      }   
      
      phiprof::stop("IO");
      addTimedBarrier("barrier-end-io");      

           
      
      //no need to propagate if we are on the final step, we just
      //wanted to make sure all IO is done even for final step
      if(P::tstep ==P::tstep_max ||
         P::t >= P::t_max) {
         break;
      }

      
      
      //Re-loadbalance if needed
      if( P::tstep%P::rebalanceInterval == 0 && P::tstep> P::tstep_min) {
         balanceLoad(mpiGrid);
         addTimedBarrier("barrier-end-load-balance");
      }
      
      //get local cells       
      vector<uint64_t> cells = mpiGrid.get_cells();      
      //compute how many spatial cells we solve for this step
      computedCells=0;
      for(uint i=0;i<cells.size();i++)  computedCells+=mpiGrid[cells[i]]->number_of_blocks*WID3;
      computedTotalCells+=computedCells;
      
      //Check if dt needs to be changed, and propagate half-steps properly to change dt and set up new situation
      //do not compute new dt on first step (in restarts dt comes from file, otherwise it was initialized before we entered
      //simulation loop
      if(P::dynamicTimestep  && P::tstep> P::tstep_min) {
         computeNewTimeStep(mpiGrid,newDt,dtIsChanged);
         addTimedBarrier("barrier-check-dt");
         if(dtIsChanged) {
            phiprof::start("update-dt");
            //propagate velocity space to real-time
            calculateAcceleration(mpiGrid,0.5*P::dt);
            //re-compute moments for real time for fieldsolver, and
            //shift compute rho_dt2 as average of old rho and new
            //rho. In practice this value is at a 1/4 timestep, as we
            //take 1/2 timestep forward in fieldsolver
            if(updateVelocityBlocksAfterAcceleration){
               //need to do a update of block lists as all cells have made local changes
               updateRemoteVelocityBlockLists(mpiGrid);
               adjustVelocityBlocks(mpiGrid);
            }

#pragma omp parallel for
            for (size_t c=0; c<cells.size(); ++c) {
               const CellID cellID = cells[c];
               SpatialCell* SC = mpiGrid[cellID];
               SC->parameters[CellParams::RHO_DT2] = 0.5*SC->parameters[CellParams::RHO];
               SC->parameters[CellParams::RHOVX_DT2] = 0.5*SC->parameters[CellParams::RHOVX];
               SC->parameters[CellParams::RHOVY_DT2] = 0.5*SC->parameters[CellParams::RHOVY];
               SC->parameters[CellParams::RHOVZ_DT2] = 0.5*SC->parameters[CellParams::RHOVZ];
               calculateCellVelocityMoments(SC);
               SC->parameters[CellParams::RHO_DT2] += 0.5*SC->parameters[CellParams::RHO];
               SC->parameters[CellParams::RHOVX_DT2] += 0.5*SC->parameters[CellParams::RHOVX];
               SC->parameters[CellParams::RHOVY_DT2] += 0.5*SC->parameters[CellParams::RHOVY];
               SC->parameters[CellParams::RHOVZ_DT2] += 0.5*SC->parameters[CellParams::RHOVZ];
            }
            
            
            // Propagate fields forward in time by 0.5*dt
            if (P::propagateField == true) {
               phiprof::start("Propagate Fields");
               propagateFields(mpiGrid, sysBoundaries, 0.5*P::dt);
               phiprof::stop("Propagate Fields",cells.size(),"SpatialCells");
            } else {
               // TODO Whatever field updating/volume
               // averaging/etc. needed in test particle and other
               // test cases have to be put here.  In doing this be
               // sure the needed components have been updated.
            }
            ++P::tstep;
            P::t += P::dt*0.5;
            P::dt=newDt;
            
            //go forward by dt/2 again in x
            calculateSpatialFluxes(mpiGrid, sysBoundaries, 0.5*P::dt);
            calculateSpatialPropagation(mpiGrid);
            logFile <<" dt changed to "<<P::dt <<"s, distribution function was half-stepped to real-time and back"<<endl<<writeVerbose;
            phiprof::stop("update-dt");
            continue; //
            addTimedBarrier("barrier-new-dt-set");
         }

      }
      
      
      phiprof::start("Propagate");
      //Propagate the state of simulation forward in time by dt:
      if (P::propagateVlasov == true) {
         phiprof::start("Propagate Vlasov");
         phiprof::start("Velocity-space");
         calculateAcceleration(mpiGrid,P::dt);
         computedCellsWithSubsteps=0;
         for(uint i=0;i<cells.size();i++)
            computedCellsWithSubsteps+=mpiGrid[cells[i]]->number_of_blocks*WID3*mpiGrid[cells[i]]->subStepsAcceleration;
         phiprof::stop("Velocity-space",computedCellsWithSubsteps,"Cells");
         addTimedBarrier("barrier-after-acceleration");

         if(updateVelocityBlocksAfterAcceleration){
            //need to do a update of block lists as all cells have made local changes
            updateRemoteVelocityBlockLists(mpiGrid);
            adjustVelocityBlocks(mpiGrid);
            addTimedBarrier("barrier-after-adjust-blocks");
         }
         
         calculateInterpolatedVelocityMoments(
            mpiGrid,
            CellParams::RHO_DT2,
            CellParams::RHOVX_DT2,
            CellParams::RHOVY_DT2,
            CellParams::RHOVZ_DT2);

         phiprof::start("Update system boundaries (Vlasov)");
         sysBoundaries.applySysBoundaryVlasovConditions(mpiGrid, P::t+0.5*P::dt); 
         phiprof::stop("Update system boundaries (Vlasov)");
         addTimedBarrier("barrier-boundary-conditions");
                  
         phiprof::start("Spatial-space");
         calculateSpatialFluxes(mpiGrid, sysBoundaries, P::dt);
         addTimedBarrier("barrier-spatial-Fluxes");
         calculateSpatialPropagation(mpiGrid);
         addTimedBarrier("barrier-spatial-propagation");
         phiprof::stop("Spatial-space",computedCells,"Cells");

         calculateInterpolatedVelocityMoments(
            mpiGrid,
            CellParams::RHO,
            CellParams::RHOVX,
            CellParams::RHOVY,
            CellParams::RHOVZ);
         
         if(!updateVelocityBlocksAfterAcceleration){
            //if no semi-lagrangean or substepping in leveque   
            //acceleration then we do the adjustment here. In that
            //case no local block adjustments have been done, so
            //remote velocty blocks do not need to be updated
            if(P::tstep%P::blockAdjustmentInterval == 0){
               adjustVelocityBlocks(mpiGrid);
            }
         }
         phiprof::stop("Propagate Vlasov",computedCells,"Cells");
      }
      
      if(P::replaceNegativeDensityCells == true) {
         #pragma omp parallel for
         for (size_t c=0; c<cells.size(); ++c) {
            const CellID cellID = cells[c];
            SpatialCell* SC = mpiGrid[cellID];
            if(SC->sysBoundaryFlag != sysboundarytype::NOT_SYSBOUNDARY) {
               continue;
            }
            if(SC->parameters[CellParams::RHO] <= 0.0) {
               CellID saneNeighbor = INVALID_CELLID;
               bool found = false;
               for(int i=-1; i<2; i++) {
                  for(int j=-1; j<2; j++) {
                     for(int k=-1; k<2; k++) {
                        const CellID cell = getNeighbour(mpiGrid,cellID,i,j,k);
                        if(cell != INVALID_CELLID) {
                           if(mpiGrid[cell]->sysBoundaryFlag == sysboundarytype::NOT_SYSBOUNDARY) {
                              if(mpiGrid[cell]->parameters[CellParams::RHO] > 0.0) {
                                 saneNeighbor = cell;
                                 found = true;
                              }
                           }
                        }
                        if(found) break;
                     }
                     if(found) break;
                  }
                  if(found) break;
               }
               if(found == false) {
                  cerr << __FILE__ << ":" << __LINE__ << ":" << "ERROR: all neighbours are insane!" << endl;
               } else {
                  SpatialCell* savior = mpiGrid[saneNeighbor];
                  logFile << "WARNING: Rescuing cell " << cellID << " as it has negative density!" << endl<<writeVerbose;
                  for(int i=CellParams::EX; i<CellParams::N_SPATIAL_CELL_PARAMS; i++) {
                     SC->parameters[i] = savior->parameters[i];
                  }
                  /*prepare list of blocks to remove. It is not safe to loop over
                   * velocity_block_list while adding/removing blocks*/
                  std::vector<uint> blocksToRemove;
                  for(uint block_i=0;
                      block_i<SC->number_of_blocks;
                  block_i++) {
                     cuint blockID=SC->velocity_block_list[block_i];
                     if(savior->is_null_block(savior->at(blockID))) {
                        //this block does not exist in savior -> mark for removal.
                        blocksToRemove.push_back(blockID);
                     }
                  }
                  
                  /*remove blocks*/
                  for(uint block_i=0;
                      block_i<blocksToRemove.size();
                  block_i++) {
                     cuint blockID=blocksToRemove[block_i];
                     SC->remove_velocity_block(blockID);
                  }
                  
                  /*add blocks*/
                  for(uint block_i=0;
                      block_i<savior->number_of_blocks;
                  block_i++) {
                     cuint blockID = savior->velocity_block_list[block_i];
                     
                     SC->add_velocity_block(blockID);          
                     const Velocity_Block* toBlock = SC->at(blockID);
                     const Velocity_Block* fromBlock = savior->at(blockID);
                     for (unsigned int i = 0; i < VELOCITY_BLOCK_LENGTH; i++) {
                        toBlock->data[i] = fromBlock->data[i];
                     }
                  }
               }
               
            }
         }
      }
      
      // Propagate fields forward in time by dt.
      if (P::propagateField == true) {
         phiprof::start("Propagate Fields");
         propagateFields(mpiGrid, sysBoundaries, P::dt);
         phiprof::stop("Propagate Fields",cells.size(),"SpatialCells");
         addTimedBarrier("barrier-after-field-solver");
      } else {
         // TODO Whatever field updating/volume averaging/etc. needed in test particle and other test cases have to be put here.
         // In doing this be sure the needed components have been updated.
      }
      
      phiprof::stop("Propagate",computedCells,"Cells");
      
      //Move forward in time      
      ++P::tstep;
      P::t += P::dt;
   }

   double after = MPI_Wtime();
   
   phiprof::stop("Simulation");
   phiprof::start("Finalization");   
   finalizeMover();
   finalizeFieldPropagator(mpiGrid);
   
   if (myRank == MASTER_RANK) {
      double timePerStep;
      if(P::tstep == P::tstep_min) {
         timePerStep=0.0;
      } else {
         timePerStep=double(after  - startTime) / (P::tstep-P::tstep_min);	
      }
      double timePerSecond=double(after  - startTime) / (P::t-P::t_min+DT_EPSILON);
      logFile << "(MAIN): All timesteps calculated." << endl;
      logFile << "\t (TIME) total run time " << after - startTime << " s, total simulated time " << P::t -P::t_min<< " s" << endl;
      if(P::t != 0.0) {
         logFile << "\t (TIME) seconds per timestep " << timePerStep  <<
         ", seconds per simulated second " <<  timePerSecond << endl;
      }
      logFile << writeVerbose;
   }
   
   phiprof::stop("Finalization");   
   phiprof::stop("main");
   
   phiprof::print(MPI_COMM_WORLD,"phiprof_full");
   phiprof::print(MPI_COMM_WORLD,"phiprof_reduced",0.01);
   
   if (myRank == MASTER_RANK) logFile << "(MAIN): Exiting." << endl << writeVerbose;
   logFile.close();
   if (P::diagnosticInterval != 0) diagnostic.close();

   MPI_Finalize();
   return 0;
}
