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
#include "fix_sph_density_continuity.h"
#include "update.h"
#include "respa.h"
#include "atom.h"
#include "force.h"
#include "modify.h"
#include "comm.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "neigh_request.h"
#include "memory.h"
#include "error.h"
#include "sph_kernels.h"
#include "fix_property_atom.h"
#include "timer.h"

using namespace LAMMPS_NS;
using namespace FixConst;

/* ---------------------------------------------------------------------- */

FixSphDensityContinuity::FixSphDensityContinuity(LAMMPS *lmp, int narg, char **arg) :
  FixSph(lmp, narg, arg)
{
  int iarg = 0;

  if (iarg+3 > narg) error->all(FLERR,"Illegal fix sph/density/continuity command");

  iarg += 3;

  while (iarg < narg) {
    // kernel style
    if (strcmp(arg[iarg],"sphkernel") == 0) {
          if (iarg+2 > narg) error->all(FLERR,"Illegal fix sph/density/continuity command");

          if(kernel_style) delete []kernel_style;
          kernel_style = new char[strlen(arg[iarg+1])+1];
          strcpy(kernel_style,arg[iarg+1]);

          // check uniqueness of kernel IDs

          int flag = SPH_KERNEL_NS::sph_kernels_unique_id();
          if(flag < 0) error->all(FLERR,"Cannot proceed, sph kernels need unique IDs, check all sph_kernel_* files");

          // get kernel id

          kernel_id = SPH_KERNEL_NS::sph_kernel_id(kernel_style);
          if(kernel_id < 0) error->all(FLERR,"Illegal fix sph/density/continuity command, unknown sph kernel");

          iarg += 2;

    } else error->all(FLERR,"Illegal fix sph/density/continuity command");
  }

  time_depend = 0; // only time step is used, but not a relative time

}

/* ---------------------------------------------------------------------- */

FixSphDensityContinuity::~FixSphDensityContinuity()
{

}

/* ---------------------------------------------------------------------- */

int FixSphDensityContinuity::setmask()
{
  int mask = 0;
  mask |= POST_INTEGRATE;
  mask |= POST_INTEGRATE_RESPA;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixSphDensityContinuity::init()
{
  FixSph::init();

  dt = update->dt;
}

/* ---------------------------------------------------------------------- */

void FixSphDensityContinuity::post_integrate()
{
  //template function for using per atom or per atomtype smoothing length
  if (mass_type) post_integrate_eval<1>();
  else post_integrate_eval<0>();
}

/* ---------------------------------------------------------------------- */

void FixSphDensityContinuity::post_integrate_respa(int ilevel, int flag)
{
  if (flag) return;             // only used by NPT,NPH

  dt = step_respa[ilevel];

  if (ilevel == 0) post_integrate();

}

/* ---------------------------------------------------------------------- */

void FixSphDensityContinuity::reset_dt()
{
  dt = update->dt;
}

/* ---------------------------------------------------------------------- */

template <int MASSFLAG>
void FixSphDensityContinuity::post_integrate_eval()
{
  int i,j,ii,jj,inum,jnum,itype,jtype;
  double xtmp,ytmp,ztmp,delx,dely,delz,rsq,r,rinv,s,gradWmag;
  int *ilist,*jlist,*numneigh,**firstneigh;
  double sli,slj,slCom,slComInv,cut,delVDotDelR,imass,jmass;

  double **x = atom->x;
  double **v = atom->v;
  double *density = atom->density;
  int *mask = atom->mask;
  int nlocal = atom->nlocal;

  // TODO: Both declaration necessary?
  int *type = atom->type;       // if MASSFLAG
  double *mass = atom->mass;    // if MASSFLAG
  double *rmass = atom->rmass;  // if !MASSFLAG

  int newton_pair = force->newton_pair;

  if (igroup == atom->firstgroup) nlocal = atom->nfirst;

  // need updated ghost positions and self contributions
  timer->stamp();
  comm->forward_comm();
  if (!MASSFLAG) fppaSl->do_forward_comm();
  timer->stamp(TIME_COMM);

  if (!MASSFLAG) updatePtrs(); // get sl


  // loop over neighbors of my atoms
  inum = list->inum;
  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  for (ii = 0; ii < inum; ii++) {
    i = ilist[ii];
    if (!(mask[i] & groupbit)) continue;
    xtmp = x[i][0];
    ytmp = x[i][1];
    ztmp = x[i][2];
    jlist = firstneigh[i];
    jnum = numneigh[i];

    if (MASSFLAG) {
      itype = type[i];
      imass = mass[itype];
    } else {
      imass = rmass[i];
      sli = sl[i];
    }

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      if (!(mask[j] & groupbit)) continue;

      if (MASSFLAG) {
        jtype = type[j];
        jmass = mass[jtype];
        slCom = slComType[itype][jtype];
      } else {
        jmass = rmass[j];
        slj = sl[j];
        slCom = interpDist(sli,slj);
      }

      cut = slCom*kernel_cut;//SPH_KERNEL_NS::sph_kernel_cut(kernel_id);

      delx = xtmp - x[j][0];
      dely = ytmp - x[j][1];
      delz = ztmp - x[j][2];
      rsq = delx*delx + dely*dely + delz*delz;

      // TODO: Cutsq from pair?
      if (rsq >= cut*cut) continue;

      // calculate normalized distance

      r = sqrt(rsq);
      if (r == 0.) {
        error->one(FLERR,"Zero distance between SPH particles!");
      }
      rinv = 1./r;
      slComInv = 1./slCom;
      s = r * slComInv;

      //    scalar product of delV and delR/R
      delVDotDelR = rinv * ( delx*(v[i][0]-v[j][0]) + dely*(v[i][1]-v[j][1]) + delz*(v[i][2]-v[j][2]) );

      // calculate value for magnitude of grad W
      gradWmag = SPH_KERNEL_NS::sph_kernel_der(kernel_id,s,slCom,slComInv);

      // add contribution of neighbor
      // have a half neigh list, so do it for both if necessary

      density[i] += calcDensityDer(delVDotDelR,gradWmag,jmass);

      if (newton_pair || j < nlocal) {
        density[j] += calcDensityDer(delVDotDelR,gradWmag,imass);
      }
    }
  }

  // density is now correct, send to ghosts
  timer->stamp();
  comm->forward_comm();
  timer->stamp(TIME_COMM);

}

/* ---------------------------------------------------------------------- */

double FixSphDensityContinuity::calcDensityDer(double delVDotDelR,
    double gradWmag, double mass)
{
  // return contribution of neighbor
  return (dt * mass * delVDotDelR * gradWmag);
}