/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations

   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com

   Christoph Kloss, christoph.kloss@cfdem.com
   Copyright 2009-2012 JKU Linz
   Copyright 2012-     DCS Computing GmbH, Linz

   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   This software is distributed under the GNU General Public License.

   See the README file in the top-level directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
Contributing author for SPH:
Andreas Aigner (CD Lab Particulate Flow Modelling, JKU)
andreas.aigner@jku.at
------------------------------------------------------------------------- */

#include "math.h"
#include "mpi.h"
#include "string.h"
#include "stdlib.h"
#include "fix_sph_pressure.h"
#include "update.h"
#include "respa.h"
#include "atom.h"
#include "force.h"
#include "modify.h"
#include "pair.h"
#include "comm.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "neigh_request.h"
#include "memory.h"
#include "error.h"
#include "timer.h"

using namespace LAMMPS_NS;
using namespace FixConst;

/* ---------------------------------------------------------------------- */

FixSPHPressure::FixSPHPressure(LAMMPS *lmp, int narg, char **arg) :
  FixSph(lmp, narg, arg)
{
    //Check args
    int iarg = 3;
    if (narg < iarg+1) error->fix_error(FLERR,this,"Not enough arguments \n");

    if (strcmp(arg[iarg],"absolut") == 0)
    {
      if (narg < iarg+2) error->fix_error(FLERR,this,"Not enough arguments for 'absolut' pressure style \n");
      B = force->numeric(arg[iarg+1]);
      pressureStyle = PRESSURESTYLE_ABSOLUT;
      iarg += 2;
    }
    else if (strcmp(arg[iarg],"Tait") == 0)
    {
      if (narg < iarg+4) error->fix_error(FLERR,this,"Not enough arguments for 'Tait' pressure style \n");
      B = force->numeric(arg[iarg+1]);
      density0 = force->numeric(arg[iarg+2]);
      if (density0 > 0) density0inv = 1./density0;
      else error->fix_error(FLERR,this," density0 is zero or negativ \n");
      gamma = force->numeric(arg[iarg+3]);
      pressureStyle = PRESSURESTYLE_TAIT;
      iarg += 4;
    }
    else if (strcmp(arg[iarg],"relativ") == 0)
    {
      if (narg < iarg+3) error->fix_error(FLERR,this,"Not enough arguments for 'relativ' pressure style \n");
      B = force->numeric(arg[iarg+1]);
      density0 = force->numeric(arg[iarg+2]);
      pressureStyle = PRESSURESTYLE_RELATIV;
      iarg += 3;
    }
    else error->fix_error(FLERR,this,"Unknown style. Valid styles are 'absolut' or 'Tait' \n");

    kernel_flag = 0; // does not need any kernel
}

/* ---------------------------------------------------------------------- */

FixSPHPressure::~FixSPHPressure()
{

}

/* ---------------------------------------------------------------------- */

int FixSPHPressure::setmask()
{
  int mask = 0;
  mask |= PRE_FORCE;
  return mask;
}

/* ---------------------------------------------------------------------- */

// TODO: post_create with per atom property

/* ---------------------------------------------------------------------- */

void FixSPHPressure::init()
{
  FixSph::init();

  // check if there is an sph/density fix present
  // must come before me, because -- (sph/density has post_integrate routine... it will be called first)
  // a - need the pressure for the density
  // b - does the forward comm for me to have updated ghost properties
  // check is done by fix sph/density itself

  int dens = -1;
  for(int i = 0; i < modify->nfix; i++)
  {
    if(strncmp("sph/density",modify->fix[i]->style,11) == 0) {
      dens = i;
      break;
    }
  }

  if(dens == -1) error->fix_error(FLERR,this,"Requires to define a fix sph/density also \n");
}

/* ---------------------------------------------------------------------- */

void FixSPHPressure::pre_force(int vflag)
{
  int *mask = atom->mask;
  double *density = atom->density;
  double *q = atom->q;
  int nlocal = atom->nlocal;

  // already have updated ghost positions

  // set pressure
  // TODO: Switch case!
  if (pressureStyle == PRESSURESTYLE_TAIT)
  {
    for (int i = 0; i < nlocal; i++)
    {
      //XXX: mask and groupbit..?!
      if (mask[i] & groupbit)
      {
        // TODO: density0 a atom type property?
      q[i] = B*(pow(density[i]*density0inv,gamma) - 1); // Tait's equation
    }
    }
  }
  else if (pressureStyle == PRESSURESTYLE_RELATIV)
  {
    for (int i = 0; i < nlocal; i++)
    {
      if (mask[i] & groupbit)
      {
        q[i] = B * (density[i] - density0);
      }
    }
  }
  else if (pressureStyle == PRESSURESTYLE_ABSOLUT)
  {
    for (int i = 0; i < nlocal; i++)
    {
      if (mask[i] & groupbit)
      {
        q[i] = B * B * density[i];//0.1 * density[i] * density[i];
      }
    }
  }

  // send pressure to ghosts
  timer->stamp();
  comm->forward_comm();
  timer->stamp(TIME_COMM);

}