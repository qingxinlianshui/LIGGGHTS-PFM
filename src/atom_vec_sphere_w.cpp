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

/*NP
   dummy functions for wedge communication
   real functions are in atom_vec_sphere_w.cpp

   DOMAIN_WEDGE_REAL_H is defined in domain_wedge.h,
   but not domain_wedge_dummy.h

   relies on the fact that release routine will only include
   atom_vec_sphere_wedge.cpp and domain_wedge.h/cpp together
*/

#include "atom_vec_sphere.h"
#include "domain_wedge.h"

#ifndef DOMAIN_WEDGE_REAL_H
#define DOMAIN_WEDGE_REAL_H

using namespace LAMMPS_NS;

/* ---------------------------------------------------------------------- */

int AtomVecSphere::pack_border_vel_wedge(int n, int *list, double *buf,
                                     int pbc_flag, int *pbc)
{
    return 0;
}

/* ---------------------------------------------------------------------- */

int AtomVecSphere::pack_comm_vel_wedge(int n, int *list, double *buf,
                                   int pbc_flag, int *pbc)
{
    return 0;
}

#endif
