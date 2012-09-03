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

#ifndef SETBYUSER_H
#define SETBYUSER_H

#include <vector>
#include "../definitions.h"
#include "../readparameters.h"
#include "../spatial_cell.hpp"
#include "sysboundarycondition.h"

namespace SBC {
   /*!\brief Base class for system boundary conditions with user-set settings and parameters read from file.
    * 
    * SetByUser is a base class for e.g. SysBoundaryConditon::SetMaxwellian.
    * It defines the managing functions to set boundary conditions on the faces of the
    * simulation domain.
    * 
    * This class handles the import and interpolation in time of the input parameters read
    * from file as well as the assignment of the state from the template cells.
    * 
    * The daughter classes have then to handle parameters and generate the template cells as
    * wished from the data returned. 
    */
   class SetByUser: public SysBoundaryCondition {
   public:
      SetByUser();
      virtual ~SetByUser();
      
      static void addParameters();
      virtual void getParameters();
      
      bool initSysBoundary(creal& t);
      int assignSysBoundary(creal* cellParams);
      bool applyInitialState(dccrg::Dccrg<SpatialCell>& mpiGrid);
      virtual void getFaces(bool* faces);
      
      virtual std::string getName() const;
      virtual uint getIndex() const;
      
   protected:
      bool loadInputData();
      std::vector<std::vector<Real> > loadFile(const char* file);
      void interpolate(const int inputDataIndex, creal t, Real* outputData);
      
      bool generateTemplateCells(creal& t);
      virtual void generateTemplateCell(spatial_cell::SpatialCell& templateCell, int inputDataIndex, creal& t);
      
      /*! Array of bool telling which faces are going to be processed by the system boundary condition.*/
      bool facesToProcess[6];
      /*! Array of bool used to tell on which face(s) (if any) a given cell is. \sa determineFace */
      bool isThisCellOnAFace[6];
      /*! Vector containing a vector for each face which has the current boundary condition. Each of these vectors has one line per input data line (time point). The length of the lines is nParams.*/
      std::vector<std::vector<Real> > inputData[6];
      /*! Array of template spatial cells replicated over the corresponding simulation volume face. Only the template for an active face is actually being touched at all by the code. */
      spatial_cell::SpatialCell templateCells[6];
      /*! List of faces on which user-set boundary conditions are to be applied ([xyz][+-]). */
      std::vector<std::string> faceList;
      /*! Input files for the user-set boundary conditions. */
      std::string files[6];
      /*! Number of parameters per input file line. */
      uint nParams;
   };
}

#endif