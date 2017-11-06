﻿/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations
   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com
   Copyright 2015-     JKU Linz
   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov
   This software is distributed under the GNU General Public License.
   See the README file in the top-level directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing authors:
   Thomas Lichtenegger (JKU Linz)
   M.Efe Kinaci (JKU Linz)
------------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "stdlib.h"
#include "atom.h"
#include "update.h"
#include "respa.h"
#include "error.h"
#include "memory.h"
#include "modify.h"
#include "comm.h"
#include "math.h"
#include "vector_liggghts.h"
#include "mpi_liggghts.h"
#include "fix_chem_shrink_core.h"
#include "fix_property_atom.h"
#include "pair_gran.h"
#include "compute_pair_gran_local.h"
#include "fix_property_global.h"
#include "properties.h"
#include "property_registry.h"
#include "global_properties.h"
#include "force.h"
#include "group.h"
#include "vector_liggghts.h"
#include "math_const.h"
#include <iostream>
#include <fstream>

using namespace LAMMPS_NS;
using namespace FixConst;
using namespace MathConst;

#define SMALL   1e-10
#define Runiv   8.3144

/* ---------------------------------------------------------------------- */

FixChemShrinkCore::FixChemShrinkCore(LAMMPS *lmp, int narg, char **arg) :
    Fix(lmp, narg, arg),
    nmaxlayers_(3),
    layers_(nmaxlayers_),
    rmin_(0.001),       //  [m]
    fix_changeOfA_(0),
    fix_changeOfC_(0),
    fix_rhogas_(0),
    fix_tgas_(0),
    fix_reactionHeat_(0),
    fix_diffcoeff_(0),
    fix_nuField_(0),
    fix_partRe_(0),
    fix_molefraction_(0),
    fix_fracRed(0),
    // for debug purposes, should be deleted afterwards
    fix_Aterm(0),
    fix_Bterm(0),
    fix_Massterm(0),
    //
    fix_layerRelRad_(0),
    fix_layerMass_(0),      //  [kg]
    fix_dens_(0),           //  [kg/m^3]
    fix_molMass_(0),        //  [kg/mole]
    fix_k0_(0),             //  [m/s]
    fix_Ea_(0),             //  [J/mol] - [kg*m^2/s^2*mol]
    fix_porosity_(0),       //  [%]
    fix_tortuosity_(0),
    fix_pore_diameter_(0)  //  [m]
{
    if ((strncmp(style, "chem/shrink/core", 16) == 0) && ((!atom->radius_flag) || (!atom->rmass_flag)))
        error->all(FLERR, "Fix chem/shrink/core needs per particle radius and mass");

    // defaults
    screenflag_ =   0;
    molMass_A_  =   molMass_C_  = 0;
    speciesA    =   speciesC    = NULL;
    pmass_      =   NULL;
    rhoeff_ = NULL;
    comm_established = false;
    iarg_ = 3;

    if (narg < 11)
        error->all(FLERR, "not enough arguments");

    bool hasargs = true;

    while (iarg_ < narg && hasargs)
    {
        if (strcmp(arg[iarg_], "speciesA") == 0)
        {
            if (narg < iarg_ + 2)
                error->fix_error(FLERR, this, "not enough arguments for 'speciesA'");
            speciesA = new char[strlen(arg[iarg_ + 1])];
            strcpy(speciesA, arg[iarg_ + 1]);
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_], "molMassA") == 0)
        {
            if (iarg_ + 2 > narg)
                error->fix_error(FLERR, this, "Wrong number of arguments");
            molMass_A_ = atof(arg[iarg_ + 1]);
            if (molMass_A_ < 0)
                error->fix_error(FLERR, this, "molar mass of A is not defined");
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_], "speciesC") == 0)
        {
            if (iarg_ + 2 > narg)
                error->fix_error(FLERR, this, "not enough arguments for 'speciesC'");
            speciesC = new char[strlen(arg[iarg_ + 1])];
            strcpy(speciesC, arg[iarg_ + 1]);
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_], "molMassC") == 0)
        {
            if (iarg_ + 2 > narg)
                error->fix_error(FLERR, this, "Wrong number of arguments");
            molMass_C_ = atof(arg[iarg_ + 1]);
            if (molMass_C_ < 0)
                error->fix_error(FLERR, this, "molar mass of C is not defined");
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_],"screen") == 0)
        {
            if (iarg_+2 > narg) error->all(FLERR,"Illegal fix/chem/shrink command");
            if (strcmp(arg[iarg_+1],"yes") == 0) screenflag_ = 1;
            else if (strcmp(arg[iarg_+1],"no") == 0) screenflag_ = 0;
            else error->all(FLERR,"Illegal fix/chem/shrink command");
            hasargs = true;
            iarg_ += 2;
        }
        else if (strcmp(arg[iarg_],"nevery") == 0)
        {
            nevery = atoi(arg[iarg_+1]);
            if (nevery <= 0) error->fix_error(FLERR,this,"");
            iarg_+=2;
            hasargs = true;
        }
        else if (strcmp(this->style,"chem/shrink") == 0)
        {
            error->fix_error(FLERR,this,"necessary keyword is missing");
        }
    }

    // define changed species mass A
    massA = new char [strlen("Modified_")+strlen(speciesA)+1];
    strcpy(massA,"Modified_");
    strcat(massA,speciesA);

    // define changed species mass C
    massC = new char [strlen("Modified_")+strlen(speciesC)+1];
    strcpy(massC,"Modified_");
    strcat(massC,speciesC);

    // define diffusant species
    diffA = new char [strlen(speciesA)+strlen("_diffCoeff")+1];
    strcpy(diffA,speciesA);
    strcat(diffA,"_diffCoeff");

    // reacting species bulk molar fraction
    moleFrac = new char [strlen("X_")+strlen(speciesA)+1];
    strcpy(moleFrac,"X_");
    strcat(moleFrac,speciesA);

    // nevery = 1;
    // time_depend = 1;
    // force_reneighbor = 1;
    next_reneighbor = update -> ntimestep + nevery;
    ts_create_  =   update->ntimestep;
}

/* ---------------------------------------------------------------------- */

FixChemShrinkCore::~FixChemShrinkCore()
{
    delete massA;
    delete massC;
    delete diffA;
    delete moleFrac;

    delete speciesA;
    delete speciesC;

    memory->destroy(rhoeff_);
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::pre_delete(bool unfixflag)
{
    if (unfixflag)
    {
        if (fix_changeOfA_)     modify  ->  delete_fix(massA);
        if (fix_changeOfC_)     modify  ->  delete_fix(massC);
        if (fix_rhogas_)        modify  ->  delete_fix("partRho");
        if (fix_tgas_)          modify  ->  delete_fix("partTemp");
        if (fix_reactionHeat_)  modify  ->  delete_fix("reactionHeat");
        if (fix_diffcoeff_)     modify  ->  delete_fix(diffA);
        if (fix_nuField_)       modify  ->  delete_fix("partNu");
        if (fix_partRe_)        modify  ->  delete_fix("partRe");
        if (fix_molefraction_)  modify  ->  delete_fix(moleFrac);
        if (fix_fracRed)        modify  ->  delete_fix("fracRed");
        // debug info print out
        if (fix_Aterm)          modify -> delete_fix("Aterm");
        if (fix_Bterm)          modify -> delete_fix("Bterm");
        if (fix_Massterm)          modify -> delete_fix("Massterm");

        if (fix_layerRelRad_) modify->  delete_fix("relRadii");
        if (fix_layerMass_)   modify -> delete_fix("massLayer");

        if (fix_porosity_)  modify -> delete_fix("porosity_");
        // if (fix_tortuosity_)    modify -> delete_fix("tortuosity_");
    }
}

/* ---------------------------------------------------------------------- */

int FixChemShrinkCore::setmask()
{
  int mask = 0;
  mask |= POST_FORCE;
  return mask;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::post_create()
{
    if (!fix_layerRelRad_)
    {
        const char* fixarg[12];
        fixarg[0]="layerRelRad";        // fixid
        fixarg[1]="all";
        fixarg[2]="property/atom";
        fixarg[3]="relRadii";           // propertyid
        fixarg[4]="vector";
        fixarg[5]="yes";
        fixarg[6]="no";
        fixarg[7]="no";
        fixarg[8]="1.0";
        fixarg[9]="0.6";
        fixarg[10]="0.5";
        fixarg[11]="0.3";
        modify->add_fix(12,const_cast<char**>(fixarg));
        fix_layerRelRad_ =  static_cast<FixPropertyAtom*>(modify->find_fix_property("relRadii","property/atom","vector",atom->ntypes,0,style));
    }

    // check for *fix_layerMass_
    if (!fix_layerMass_)
    {
        const char* fixarg[12];
        fixarg[0]="massLayer";
        // fixarg[1]=group->names[igroup];
        fixarg[1]="all";
        fixarg[2]="property/atom";
        fixarg[3]="massLayer";
        fixarg[4]="vector";
        fixarg[5]="yes";
        fixarg[6]="no";
        fixarg[7]="no";
        fixarg[8]="0.0";
        fixarg[9]="0.0";
        fixarg[10]="0.0";
        fixarg[11]="0.0";
        modify->add_fix(12,const_cast<char**>(fixarg));
        fix_layerMass_ = static_cast<FixPropertyAtom*>(modify->find_fix_property("massLayer","property/atom","vector",atom->ntypes,0,style));
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::updatePtrs()
{
    changeOfA_      =   fix_changeOfA_  ->  vector_atom;
    changeOfC_      =   fix_changeOfC_  ->  vector_atom;
    rhogas_         =   fix_rhogas_     ->  vector_atom;
    T_              =   fix_tgas_       ->  vector_atom;
    reactionHeat_   =   fix_reactionHeat_-> vector_atom;
    // handle for diffusion coefficient
    molecularDiffusion_     =   fix_diffcoeff_  ->  vector_atom;
    // handles for viscosity field and reynolds number
    nuf_            =   fix_nuField_    ->  vector_atom;
    Rep_            =   fix_partRe_     ->  vector_atom;
    // handle for molar fraction
    X0_             =   fix_molefraction_   -> vector_atom;

    // handle for relative radius
    relRadii_       =   fix_layerRelRad_    -> array_atom;
    // mass handle
    massLayer_      = fix_layerMass_    ->  array_atom;
    // density handle
    layerDensities_ =   fix_dens_       -> get_values();
    // molar mass handle
    layerMolMasses_ = fix_molMass_      -> get_values();

    // chemical prop
    // frequency factor
    k0_             =  fix_k0_          -> get_values();
    // activation energy
    Ea_             =  fix_Ea_          -> get_values();

    // particle porosity properties
    porosity_       = fix_porosity_ ->array_atom;
    // porosity_       =   fix_porosity_       -> get_values();
    tortuosity_     =   fix_tortuosity_     -> get_values();
    // tortuosity_     =   fix_tortuosity_     -> array_atom;
    pore_diameter_  =   fix_pore_diameter_  -> get_values();

    pmass_          =   atom    -> rmass;
    pdensity_       =   atom    -> density;
    TimeStep        =   update  -> dt;
    radius_         =   atom    -> radius;

    fracRed_        =   fix_fracRed ->array_atom;
    // for debugging
    Aterm           =   fix_Aterm   ->array_atom;
    Bterm           =   fix_Bterm   ->array_atom;
    Massterm        =   fix_Massterm->vector_atom;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::init()
{
    // error checks
    if (!atom->radius_flag)
      error->fix_error(FLERR,this,"requires atom attribute radius (per-particle)");
    if (!atom->rmass_flag)
      error->fix_error(FLERR,this,"requires atom attribute mass (per-particle)");
    if (!atom->tag_enable || 0 == atom->map_style)
      error->fix_error(FLERR,this,"requires atom tags and an atom map");

    // find coupling fix & get coupling interval value
    fc_ = static_cast<FixCfdCoupling*>(modify->find_fix_style_strict("couple/cfd",0));
    couple = fc_ -> couple_nevery_ + 1;

    int ntype = atom -> ntypes;
    char* fixname = new char[strlen(id)+1];

    // look up pre-exponential factor k0
    // differs for every ore id
    strcpy (fixname,"k0_");
    strcat(fixname,id);
    fix_k0_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname,"property/global","vector",ntype,0,"FixChemShrinkCore"));
    // delete []fixname;

    // look up activation energies Ea
    // differs for every ore id
    // fixname = new char [strlen(id)+1];
    strcpy(fixname, "Ea_");
    strcat(fixname, id);
    fix_Ea_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname, "property/global", "vector", ntype, 0, "FixChemShrinkCore"));
    delete[]fixname;

    // Layer Molar Mass
    fixname = new char [strlen("molMass_")+strlen(group->names[igroup])];
    strcpy(fixname,"molMass_");
    strcat(fixname,group->names[igroup]);
    fix_molMass_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname,"property/global","vector",ntype,0,"FixChemShrinkCore"));
    delete []fixname;

    // Layer Density
    fixname = new char [strlen("density_")+strlen(group->names[igroup])];
    strcpy(fixname,"density_");
    strcat(fixname,group->names[igroup]);
    fix_dens_ = static_cast<FixPropertyGlobal*>(modify->find_fix_property(fixname,"property/global","vector",ntype,0,"FixChemShrinkCore"));
    delete []fixname;

    // references
    fix_changeOfA_      =   static_cast<FixPropertyAtom*>(modify->find_fix_property(massA, "property/atom", "scalar", 0, 0, id));
    fix_changeOfC_      =   static_cast<FixPropertyAtom*>(modify->find_fix_property(massC, "property/atom", "scalar", 0, 0, id));
    fix_rhogas_         =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partRho", "property/atom", "scalar", 0, 0, id));
    fix_tgas_           =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partTemp", "property/atom", "scalar", 0, 0, style));
    fix_reactionHeat_   =   static_cast<FixPropertyAtom*>(modify->find_fix_property("reactionHeat", "property/atom", "scalar", 0, 0, id));
    fix_diffcoeff_      =   static_cast<FixPropertyAtom*>(modify->find_fix_property(diffA, "property/atom", "scalar", 0, 0, id));
    fix_nuField_        =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partNu", "property/atom", "scalar", 0, 0, style));
    fix_partRe_         =   static_cast<FixPropertyAtom*>(modify->find_fix_property("partRe", "property/atom", "scalar", 0, 0, style));
    fix_molefraction_   =   static_cast<FixPropertyAtom*>(modify->find_fix_property(moleFrac, "property/atom", "scalar", 0, 0, id));
    fix_fracRed         =   static_cast<FixPropertyAtom*>(modify->find_fix_property("fracRed", "property/atom", "vector", 0, 0, style));
    // debug fixes
    fix_Aterm           =   static_cast<FixPropertyAtom*>(modify->find_fix_property("Aterm", "property/atom", "vector", 0, 0, style));
    fix_Bterm           =   static_cast<FixPropertyAtom*>(modify->find_fix_property("Bterm", "property/atom", "vector", 0, 0, style));
    fix_Massterm        =   static_cast<FixPropertyAtom*>(modify->find_fix_property("Massterm", "property/atom", "scalar", 0, 0, style));

    // lookup porosity,tortuosity and pore diameter values
    //fix_porosity_       =   static_cast<FixPropertyGlobal*>(modify->find_fix_property("porosity_", "property/global", "scalar", 0, 0, "FixChemShrinkCore"));

    fix_porosity_       =   static_cast<FixPropertyAtom*>(modify->find_fix_property("porosity_", "property/atom", "vector", 0, 0, "FixChemShrinkCore"));
    // tortuosity is calculated with the empirical correlation of Murayama et al. 1978 - thus different through every interface layer
    // fix_tortuosity_     =   static_cast<FixPropertyAtom*>(modify->find_fix_property("tortuosity_", "property/atom", "vector", 0, 0, "FixChemShrinkCore"));

    fix_tortuosity_     =   static_cast<FixPropertyGlobal*>(modify->find_fix_property("tortuosity_", "property/global", "scalar", 0, 0, "FixChemShrinkCore"));
    fix_pore_diameter_  =   static_cast<FixPropertyGlobal*>(modify->find_fix_property("pore_diameter_", "property/global", "scalar", 0, 0, "FixChemShrinkCore"));

    updatePtrs();
    memory->create(rhoeff_,atom->nmax,nmaxlayers_+1,"rhoeff_");

    for (int i = 0; i < atom->nlocal; ++i)
    {
        calcRhoEff_(i);
        calcMassLayer(i);
        if (screenflag_ && screen)
        {
            fprintf(screen, "dCoeff value is : %.10f \n", molecularDiffusion_[i]);
        }
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::post_force(int)
{
    updatePtrs();
    int i;
    int nlocal  =   atom->nlocal;
    int *mask   =   atom->mask;
    double a_[nmaxlayers_];              // reaction resistance value for each layer
    double x0_eq_[nmaxlayers_];          // molar fraction of reactant gas
    double b_[nmaxlayers_];              // diffusion resistance value
    double dmA_[nmaxlayers_];            // mass flow rate of reactant gas species for each layer
    double masst_[nlocal];
    ts = update->ntimestep;

    // do chem/shrink/core calculations if communication between CFDEM and LIGGGHTS already happened
    // need initial values from CFDEM side
    if (!comm_established)
    {
        if (ts > ts_create_ + couple)
        {
            comm_established = true;
        }
    }

    if (comm_established)
    {
        for (i = 0; i < nlocal; i++)
        {
            if (mask[i] & groupbit)
            {
                // calcRhoEff_(i);
                active_layers(i);
                FractionalReduction(i);
                if (layers_ > 0)
                {
                    getXi(i,x0_eq_);
                    for (int j = 0; j < nmaxlayers_; j++)
                    {
                        a_[j] = 0.0;
                        dmA_[j] = 0.0;
                    }
                    getA(i,a_);
                    getB(i,b_);
                    getMassT(i,masst_);
                    reaction(i, a_, dmA_,x0_eq_, b_, masst_);
                    // reaction2(i,a_,dmA_,x0_eq_, b_);
                    // reaction3(i,a_,dmA_,x0_eq_);
                    update_atom_properties(i,dmA_);
                    update_gas_properties(i,dmA_);
                }
            }
        }
    }

    bigint nblocal = atom->nlocal;
    MPI_Allreduce(&nblocal,&atom->natoms,1,MPI_LMP_BIGINT,MPI_SUM,world);

    //NP tags and maps
    if (atom->molecular == 0) {
    int *tag = atom->tag;
    for (i = 0; i < atom->nlocal; i++) tag[i] = 0;
    atom->tag_extend();
    }

    if (atom->tag_enable) {
      if (atom->map_style) {
        atom->nghost = 0;
        atom->map_init();
        atom->map_set();
      }
    }
}

/* ----------------- compute particle surface area ------------------------ */

double FixChemShrinkCore::partSurfArea(double radius)
{
     double A_p =   4.0*M_PI*radius*radius;
     return (A_p);
}

/* ---------------------------------------------------------------------- */

// returns number of active layers
// calculate the new masses of layers, (important if layer number has changed)
int FixChemShrinkCore::active_layers(int i)
{
    for(int j  = layers_; j >= 1; j--)
    {
        if (relRadii_[i][j]*radius_[i] < rmin_)
        {
            layers_ -= 1;
            calcMassLayer(i);
            break;
        }
    }
    if (screen)
        fprintf(screen, "active layers: %i \n", layers_);
    return layers_;
}

/* ---------------------------------------------------------------------- */

// Determine the mass for every layer -- the latest layer represents the core of the particle.
void FixChemShrinkCore::calcMassLayer(int i)
{

    double rad = radius_[i];
    // massLayer_[i][layers_]   =   1.33333*M_PI*relRadii_[i][layers_]*relRadii_[i][layers_]*relRadii_[i][layers_]*layerDensities_[layers_];
    massLayer_[i][layers_]   =   1.33333*M_PI*relRadii_[i][layers_]*relRadii_[i][layers_]*relRadii_[i][layers_]*rhoeff_[i][layers_];
    massLayer_[i][layers_]   *= rad*rad*rad;
    for (int j=0; j < layers_; j++)
    {
        // massLayer_[i][j]   =   1.33333*M_PI*((relRadii_[i][j]*relRadii_[i][j]*relRadii_[i][j])-(relRadii_[i][j+1]*relRadii_[i][j+1]*relRadii_[i][j+1]))*layerDensities_[j];
        massLayer_[i][j]   =   1.33333*M_PI*((relRadii_[i][j]*relRadii_[i][j]*relRadii_[i][j])-(relRadii_[i][j+1]*relRadii_[i][j+1]*relRadii_[i][j+1]))*rhoeff_[i][j];
        massLayer_[i][j]   *= rad*rad*rad;
    }

    if (screen)
    {
        fprintf(screen, "mass layer h: %f \n", massLayer_[i][3]);
        fprintf(screen, "mass layer m: %f \n", massLayer_[i][2]);
        fprintf(screen, "mass layer w: %f \n", massLayer_[i][1]);
        fprintf(screen, "mass layer fe: %f \n", massLayer_[i][0]);
    }

}

/* ---------------------------------------------------------------------- */

// Calculate the equilibrium molar fractions
// And the bulk molar fraction
void FixChemShrinkCore::getXi(int i, double *x0_eq_)
{
    if (screen)
        fprintf(screen,"getXi \n");
    // calculate equilibrium concentration from K_eq(layer, T)
    double kc = 0.4;

    for (int j = 0; j < layers_; j++)
    {
        x0_eq_[j]  =   kc/(1+K_eq(j,T_[i]));
    }
}

/* ---------------------------------------------------------------------- */

// calculate A - the chemical reaction resistance term - starting from wustite(0).
// Equation available in literature. (Valipour, Natsui, Nietrost...)
// A_[j] [s/m]
void FixChemShrinkCore::getA(int i, double *a_)
{
    if (screen)
        fprintf(screen,"getA \n");
    for (int j = 0; j < layers_ ; j++)
    {
        // a_[j]   =   (1/(k0_[j]*exp(-Ea_[j]/(Runiv*T_[i]))))*(1/(1+1/K_eq(j,T_[i])))*(1/pow((1-fracRed_[i][j]),0.6666));
        a_[j]   =   (k0_[j]*exp(-Ea_[j]/(Runiv*T_[i])))*pow((1-fracRed_[i][j]),0.6666);
        a_[j]   =   1/a_[j];

        // aterm
        Aterm[i][j] = a_[j];
    }
}

/* ---------------------------------------------------------------------- */

// Calculate B - the diffusion resistance term - 0 = wustite
// Use binary diffusion for mixture, and knudsen diffusion to determine
// effective diffusion coefficient.
void FixChemShrinkCore::getB(int i, double *b_)
{
    if (screen)
        fprintf(screen,"getB \n");
    double effBinaryDiff_[nmaxlayers_];
    double effKnudsenDiff_[nmaxlayers_];
    double fracRedThird_[nmaxlayers_];
    diffEff_ = new double [nmaxlayers_];

    for (int j = 0; j < layers_; j++)
    {
        // calculate fractional reduction to the power of 1/3 for simpler use
        fracRedThird_[j] = pow((1-fracRed_[i][j]),THIRD);

        // Calculate the effective molecular diffusion
        effBinaryDiff_[j] = molecularDiffusion_[i]*(porosity_[i][j]/tortuosity_[i]) + 1e-18;

        // Calculate the knudsen diffusion
        effKnudsenDiff_[j]  =   (pore_diameter_[i]/6.)*sqrt((8*Runiv*T_[i])/(M_PI*molMass_A_))*(porosity_[i][j]/tortuosity_[i]) + 1e-18;
    }

    // total effective diffusivity
    for (int j = 0; j < layers_ ; j++)
    {
        // diffEff_[j] = ((effBinaryDiff_[j]*effKnudsenDiff_[j])/(effBinaryDiff_[j]+effKnudsenDiff_[j]));
        diffEff_[j] =   1.0/(1.0/effBinaryDiff_[j] + 1.0/effKnudsenDiff_[j]);
    }

    // calculation of Diffustion Term from Valipour 2009
    // from wustite to iron
    b_[0]   =   ((1-fracRedThird_[0])/fracRedThird_[0])*(radius_[i]/diffEff_[0]);

    for (int j = 1; j <layers_; j++)
    {
        b_[j] = (fracRedThird_[j-1]-fracRedThird_[j])/(fracRedThird_[j-1]*fracRedThird_[j])*(radius_[i]/diffEff_[j]);
    }

    for (int k = 0; k < layers_; ++k)
    {
        Bterm[i][k] = b_[k];
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::getMassT(int i, double *masst_)
{
    if (screen)
        fprintf(screen,"getMassT \n");
    // initialize sherwood & schmidt numbers for every particle
    double Sc_[atom->nlocal];
    double Sh_[atom->nlocal];

    Sc_[i]  =   nuf_[i]/molecularDiffusion_[i];
    // Sh_[i]  =   2+0.6*pow(Rep_[i],0.5)*pow(Sc_[i],THIRD);
    Sh_[i] = 0.664*pow(Rep_[i],0.5)*pow(Sc_[i],THIRD);

    masst_[i] = Sh_[i]*molecularDiffusion_[i]/(2*radius_[i]);
    masst_[i] = 1/masst_[i];

    Massterm[i] = masst_[i];
}
/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::reaction(int i, double *a_, double *dmA_, double *x0_eq_, double *b_, double *masst_)
{
    if (screen)
        fprintf(screen,"reaction \n");
    updatePtrs();
    double W;
    double dY[nmaxlayers_];

    if (layers_ == nmaxlayers_)
    {
        // including reaction resistance and diffusion coeff terms
        W = (a_[2]+b_[2])*(a_[0]*(a_[1]+b_[1]+b_[0]+masst_[i])+(a_[1]+b_[1])*(b_[0]+masst_[i]))
                +a_[1]*(a_[0]*(b_[1]+b_[0]+masst_[i])+b_[1]*(b_[0]+masst_[i]));
        // hematite to magnetite
        dY[2]   =   ((a_[0]*(a_[1]+b_[1]+b_[0]+masst_[i])+(b_[0]+masst_[i])*(a_[1]+b_[1]))*(X0_[i]-x0_eq_[2])
                -   (a_[0]*(b_[1]+b_[0]+masst_[i])+b_[1]*(b_[0]+masst_[i]))*(X0_[i]-x0_eq_[1])
                -   (a_[1]*(b_[0]+masst_[i]))*(X0_[i]-x0_eq_[0]))*1/W;
        // magnetite to wustite
        dY[1]   =   (((a_[2]+b_[2]+b_[1])*(a_[0]+b_[0]+masst_[i])+a_[0]*(b_[0]+masst_[i]))*(X0_[i]-x0_eq_[1])
                -   (b_[1]*(a_[0]+b_[0]+masst_[i])+a_[0]*(b_[0]+masst_[i]))*(X0_[i]-x0_eq_[2])
                -   ((a_[2]+b_[2])*(b_[0]+masst_[i]))*(X0_[i]-x0_eq_[0]))*1/W;
        // wustite to iron
        dY[0]   =   (((a_[2]+b_[2])*(a_[1]+b_[1]+b_[0]+masst_[i])+a_[1]*(b_[1]+b_[0]+masst_[i]))*(X0_[i]-x0_eq_[0])
                -   (a_[1]*(b_[0]+masst_[i]))*(X0_[i]-x0_eq_[2])
                -   ((a_[2]+b_[2])*(b_[0]+masst_[i]))*(X0_[i]-x0_eq_[1]))*1/W;
    }
     else if (layers_ == 2)
     {
        W = (a_[1]+b_[1])*(a_[0]+b_[0]+masst_[i])+a_[0]*(b_[0]+masst_[i]);

        // hematite to magnetite
        dY[2]   =   0.0;
        // magnetite to wustite
        dY[1]   =   ((a_[0]+b_[0]+masst_[i])*(X0_[i]-x0_eq_[1])-(b_[0]+masst_[i])*(X0_[i]-x0_eq_[0]))*1/W;
        // wustite to iron
        dY[0]   =   ((a_[1]+b_[1]+b_[0]+masst_[i])*(X0_[i]-x0_eq_[0])-(b_[0]+masst_[i])*(X0_[i]-x0_eq_[1]))*1/W;
     }
     else if (layers_ == 1)
     {

        // rate of chemical reaction for 1 active layer
        W = a_[0]+b_[0]+masst_[i];

        // hematite to magnetite
        dY[2]   =   0.0;
        // magnetite to wustite
        dY[1]   =   0.0;
        // wustite to iron
        dY[0]   =   (X0_[i] - x0_eq_[0])*1/W;
     }

    for (int j = 0 ; j < nmaxlayers_; j++)
    {
        // mass flow rate for reactant gas species
        dmA_[j] =   dY[j]*rhogas_[i]*partSurfArea(radius_[i])*TimeStep*nevery;
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::reaction2(int i, double *a_, double *dmA_, double *x0_eq_, double *b_) // double masst
{
    updatePtrs();
    double W;
    double dY[nmaxlayers_];

    if (layers_ == nmaxlayers_)
    {
        // including reaction resistance and diffusion coeff terms
        W = (a_[2]+b_[2])*(a_[0]*(a_[1]+b_[1]+b_[0])+(a_[1]+b_[1])*b_[0])
                +a_[1]*(a_[0]*(b_[1]+b_[0])+b_[1]*b_[0]);
        // hematite to magnetite
        dY[2]   =   ((a_[0]*(a_[1]+b_[1]+b_[0])+b_[0]*(a_[1]+b_[1]))*(X0_[i]-x0_eq_[2])
                -   (a_[0]*(b_[1]+b_[0])+b_[1]*b_[0])*(X0_[i]-x0_eq_[1])
                -   (a_[1]*b_[0])*(X0_[i]-x0_eq_[0]))*1/W;
        // magnetite to wustite
        dY[1]   =   (((a_[2]+b_[2]+b_[1])*(a_[0]+b_[0])+a_[0]*b_[0])*(X0_[i]-x0_eq_[1])
                -   (b_[1]*(a_[0]+b_[0])+a_[0]*b_[0])*(X0_[i]-x0_eq_[2])
                -   ((a_[2]+b_[2])*b_[0])*(X0_[i]-x0_eq_[0]))*1/W;
        // wustite to iron
        dY[0]   =   (((a_[2]+b_[2])*(a_[1]+b_[1]+b_[0])+a_[1]*(b_[1]+b_[0]))*(X0_[i]-x0_eq_[0])
                -   (a_[1]*b_[0])*(X0_[i]-x0_eq_[2])
                -   ((a_[2]+b_[2])*b_[0])*(X0_[i]-x0_eq_[1]))*1/W;
    }
     else if (layers_ == 2)
     {
        W = (a_[1]+b_[1])*(a_[0]+b_[0])+a_[0]*b_[0];

        // hematite to magnetite
        dY[2]   =   0.0;
        // magnetite to wustite
        dY[1]   =   ((a_[0]+b_[0])*(X0_[i]-x0_eq_[1])-b_[0]*(X0_[i]-x0_eq_[0]))*1/W;
        // wustite to iron
        dY[0]   =   ((a_[1]+b_[1]+b_[0])*(X0_[i]-x0_eq_[0])-b_[0]*(X0_[i]-x0_eq_[1]))*1/W;
     }
     else if (layers_ == 1)
     {

        // rate of chemical reaction for 1 active layer
        W = a_[0]+b_[0];

        // hematite to magnetite
        dY[2]   =   0.0;
        // magnetite to wustite
        dY[1]   =   0.0;
        // wustite to iron
        dY[0]   =   (X0_[i] - x0_eq_[0])*1/W;
     }

    for (int j = 0 ; j < nmaxlayers_; j++)
    {
        // mass flow rate for reactant gas species
        // scaled up Timestep to match 0.01;
        dmA_[j] =   dY[j]*rhogas_[i]*partSurfArea(radius_[i])*TimeStep*nevery;
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::reaction3(int i, double *a_, double *dmA_, double *x0_eq_)
{
    updatePtrs();
    double W;
    double dY[nmaxlayers_];

    if (layers_ == nmaxlayers_)
    {
        // if only with reaction resistance term
        // rate of chemical reaction for only chemical reaction resistance.
        W = a_[2]*a_[1]*a_[0];
        // hematite to magnetite
        dY[2]   =   ((a_[0]*a_[1])*(X0_[i]-x0_eq_[2]))*1/W;
        // magnetite to wustite
        dY[1]   =   ((a_[2]*a_[0])*(X0_[i]-x0_eq_[1]))*1/W;
        // wustite to iron
        dY[0]   =   ((a_[2]*a_[1])*(X0_[i]-x0_eq_[0]))*1/W;
    }
     else if (layers_ == 2)
     {
         // rate of chemical reaction for 2 active layers
         W = (a_[1]*a_[0]);

         // hematite to magnetite
         dY[2]   =   0.0;
         // magnetite to wustite
         dY[1]   =   (a_[0]*(X0_[i]-x0_eq_[1]))*1/W;
         // wustite to iron
         dY[0]   =   (a_[1]*(X0_[i] - x0_eq_[0]))*1/W;
     }
     else if (layers_ == 1)
     {
         // rate of chemical reaction for 1 active layer
         W = a_[0];

         // hematite to magnetite
         dY[2]   =   0.0;
         // magnetite to wustite
         dY[1]   =   0.0;
         // wustite to iron
         dY[0]   =   (X0_[i] - x0_eq_[0])*1/W;
     }

    for (int j = 0 ; j < nmaxlayers_; j++)
    {
        dmA_[j] =   dY[j]*rhogas_[i]*partSurfArea(radius_[i])*TimeStep*nevery;
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::update_atom_properties(int i, double *dmA_)
{
    if (screen)
        fprintf(screen,"run update atom props \n");
    // based on material change: update relative radii, average density and mass of atom i        //  mass of each layer
    double dmL_[nmaxlayers_+1];               //  mass flow rate between each layer i.e. (btw h->m, m->w, w->Fe) must consider reduction and growth at the same time
    double sum_mass_new = 0;                  // just for control case --- delete afterwards
    double nu_reac_[3] = {1, 1, 3};
    double nu_prod_[3] = {1, 3, 2};
    double radLayer_[nmaxlayers_+1];
    double dV_[nmaxlayers_+1];      // Volume change of particle layer
    double V_[nmaxlayers_+1];       // Volume of particle layer

    // calculate initial Volume of layers from radius
    V_[layers_] = 4/3*M_PI*relRadii_[i][layers_]*relRadii_[i][layers_]*relRadii_[i][layers_];
    V_[layers_] *= radius_[i]*radius_[i]*radius_[i];
    for (int layer = layers_-1; layer >= 0; layer--)
    {
        V_[layer] = 4/3*M_PI*((relRadii_[i][layer]*relRadii_[i][layer]*relRadii_[i][layer])-(relRadii_[i][layer+1]*relRadii_[i][layer+1]*relRadii_[i][layer+1]));
        V_[layer] *= radius_[i]*radius_[i]*radius_[i];
    }

    // Calculate mass flow rates (dmL[j])
    // Keep in mind the stoichiometric coefficients of reaction prod - reactants
    dmL_[layers_] = dmA_[layers_-1]*nu_reac_[layers_-1]*(layerMolMasses_[layers_]/molMass_A_);
    for (int j = layers_ -1; j>= 1; j--)
    {
        dmL_[j] = -dmA_[j]*nu_prod_[j]*(layerMolMasses_[j]/molMass_A_) + dmA_[j-1]*nu_reac_[j-1]*(layerMolMasses_[j]/molMass_A_);
    }
    dmL_[0] = - dmA_[0]*nu_prod_[0]*(layerMolMasses_[0]/molMass_A_);

    if (layers_ == 2)
        dmL_[layers_+1] = 0.0;
    else if (layers_ == 1)
    {
        dmL_[layers_+2] = 0.0;
        dmL_[layers_+1] = 0.0;
    }

    // calculate the Volume change of layers
    dV_[layers_] = dmL_[layers_]/rhoeff_[i][layers_];
    for (int layer = layers_ - 1; layer >= 1; layer--)
    {
        dV_[layer]= -dmA_[layer]*nu_prod_[layer]*(layerMolMasses_[layer]/molMass_A_)/rhoeff_[i][layer+1] + dmA_[layer-1]*nu_reac_[layer-1]*(layerMolMasses_[layer]/molMass_A_)/rhoeff_[i][layer];
    }
    dV_[0] = dmL_[0]/rhoeff_[i][1];

    // based on chem. reactions, get new layer masses
    for (int j = 0; j <= layers_; j++)
    {
        // TL: massLayer_[j] should never be negative anyways; just make sure not more is removed than present
        massLayer_[i][j] -= dmL_[j];
        if (massLayer_[i][j] < SMALL)
                 massLayer_[i][j] = SMALL;

        V_[j] -= dV_[j];
        if (V_[j] < SMALL)
            V_[j] = SMALL;

        if (screen)
        {
            fprintf(screen, "massLayer3: %f \n", massLayer_[i][3]);
            fprintf(screen, "massLayer2: %f \n", massLayer_[i][2]);
            fprintf(screen, "massLayer1: %f \n", massLayer_[i][1]);
            fprintf(screen, "massLayer0: %f \n", massLayer_[i][0]);
        }

        sum_mass_new    +=  massLayer_[i][j];
    }

    // radLayer_[layers_]   =   pow((0.75*massLayer_[i][layers_]/(layerDensities_[layers_]*M_PI)),THIRD);
    radLayer_[layers_]   =   pow((0.75*massLayer_[i][layers_]/(rhoeff_[i][layers_]*M_PI)),THIRD);
    for (int j = layers_-1; j >= 0 ; j--)
    {
        // radLayer_[j]   =   pow((0.75*massLayer_[i][j]/(M_PI*layerDensities_[j])+radLayer_[j+1]*radLayer_[j+1]*radLayer_[j+1]),THIRD);
        radLayer_[j]   =   pow((0.75*massLayer_[i][j]/(M_PI*rhoeff_[i][j])+radLayer_[j+1]*radLayer_[j+1]*radLayer_[j+1]),THIRD);
    }
    radius_[i] = radLayer_[0];

    //detemine the new relative layer radii and store them in relRadii_
    for (int j = 0; j <= layers_; j++)
    {
        relRadii_[i][j] = radLayer_[j]/radius_[i];
    }

    // update effective layer densities depending on changed mass and volume
    for (int layer = 0; layer <= layers_; layer++)
    {
        rhoeff_[i][layer] = massLayer_[i][layer]/V_[layer];
    }

    if (screen)
    {
        fprintf(screen, "new effective density 3: %f \n",rhoeff_[i][3]);
        fprintf(screen, "new effective density 2: %f \n",rhoeff_[i][2]);
        fprintf(screen, "new effective density 1: %f \n",rhoeff_[i][1]);
        fprintf(screen, "new effective density 0: %f \n",rhoeff_[i][0]);
    }

    // from the new layer masses, get new total mass, layer radii and total density
    pmass_[i]   =   sum_mass_new;

    // calculate particle effective density
    // depending on the new values
    // update total partilce density
    pdensity_[i]    =   0.75*pmass_[i]/(M_PI*radius_[i]*radius_[i]*radius_[i]);

    // calculate new porosities for layers
    for (int layer = 0; layer <= layers_; layer++)
    {
        porosity_[i][layer] = 1 - rhoeff_[i][layer]/layerDensities_[layer];
    }

    if (screen)
    {
        fprintf(screen, "new porosity_h: %f \n", porosity_[i][3]);
        fprintf(screen, "new porosity_m: %f \n", porosity_[i][2]);
        fprintf(screen, "new porosity_w: %f \n", porosity_[i][1]);
        fprintf(screen, "new porosity_fe: %f \n", porosity_[i][0]);
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::update_gas_properties(int i, double *dmA_)
{
   // based on material change: update gas-phase source terms for mass and heat
    for (int j = 0; j < nmaxlayers_;j++)
    {
        changeOfA_[i]   -=  dmA_[j];
        changeOfC_[i]   +=  dmA_[j]*molMass_C_/molMass_A_;
    }
}

/* ---------------------------------------------------------------------- */

double FixChemShrinkCore::K_eq(int layer, double T)
{
    // ATTENTION: K is usually given for partial pressures, might be necessary to convert to molar concentrations
    // for the reactions under consideration, this should not be the case (double-check !!!)
    // 0 = wustite, 1 = mangetite, 2 = hematite;
    double Keq_ = 0.;
    if(strcmp(speciesA,"CO")==0)
     {
         if (layer == 0)
             Keq_   =   pow(10,(917/T-1.097));
         else if (layer == 1)
             Keq_   =   pow(10,(-1834/T+2.17));
         else if (layer == 2)
             Keq_   =   exp(3968.37/T+3.94);
     }
     else if(strcmp(speciesA,"H2")==0)
     {
         if (layer == 0)
             Keq_   =   pow(10,(-827/T+0.468));
         else if (layer == 1)
             Keq_   =   pow(10,(-3577/T+3.74));
         else if (layer == 2)
             Keq_   =   exp(-362.6/T+10.344);
     }
    else
     {
         printf("Error : Undefined Reaction \n");
     }
    return Keq_;
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::FractionalReduction(int i)
{
    double f_[nmaxlayers_];
    // calculate the fractional reduction as defined in Tang et al. (2012)
    // "Simulation study on performance of Z-path Moving-fluidized Bed for Gaseous Reduction
    // of Iron Ore Fines" ISIJ International, Vol. 52, No. 7, pp. 1241 - 1249
    // f_[0] = 1 - ((2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1])/(2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1]+massLayer_[i][0]/layerMolMasses_[0]));
    // f_[1] = 1 - ((2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2])/(2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1]+massLayer_[i][0]/layerMolMasses_[0]));
    //f_[2] = 1 - ((2*massLayer_[i][3]/layerMolMasses_[3])/(2*massLayer_[i][3]/layerMolMasses_[3]+3*massLayer_[i][2]/layerMolMasses_[2]+massLayer_[i][1]/layerMolMasses_[1]+massLayer_[i][0]/layerMolMasses_[0]));

    for (int k=0; k<layers_; k++)
    {
        f_[k] = 1.0 - relRadii_[i][k+1]*relRadii_[i][k+1]*relRadii_[i][k+1];
        fracRed_[i][k] = f_[k];
    }
}

/* ---------------------------------------------------------------------- */

void FixChemShrinkCore::calcRhoEff_(int i)
{
    for (int layer=0; layer <= layers_; layer++)
        rhoeff_[i][layer] = (1.0 - porosity_[i][layer])*layerDensities_[layer];

    if (screen)
    {
        fprintf(screen, "effective density 3: %f \n",rhoeff_[i][3]);
        fprintf(screen, "effective density 2: %f \n",rhoeff_[i][2]);
        fprintf(screen, "effective density 1: %f \n",rhoeff_[i][1]);
        fprintf(screen, "effective density 0: %f \n",rhoeff_[i][0]);
    }
}
