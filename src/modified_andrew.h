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
   Contributing authors:
   Stefan Amberger (JKU Linz)
------------------------------------------------------------------------- */

#ifndef LMP_MODIFIED_ANDREW_H
#define LMP_MODIFIED_ANDREW_H

#include <vector>
#include "pointers.h"

namespace MODIFIED_ANDREW_AUX{

struct Point {
  double x, y;

  bool operator <(const Point &p) const {
    return x < p.x || (x == p.x && y < p.y);
  }
};

struct Circle {
  double x, y, r;
};

}

using MODIFIED_ANDREW_AUX::Point;
using MODIFIED_ANDREW_AUX::Circle;

namespace LAMMPS_NS{

  /**
   * @brief ModifiedAndrew class
   *        Andrew's monotone chain convex hull algorithm
   */
class ModifiedAndrew : protected Pointers {

public:
  ModifiedAndrew(LAMMPS *lmp);
  ~ModifiedAndrew();

  double area();

  void add_contact(Circle c);

  inline void clear_contacts()
  { contacts_.clear(); }

private:

  double area(std::vector<Point> H);
  double area(Point &p, Point &m, Point &q);
  double cross(Point O, Point A, Point B);

  Point mean_point(std::vector<Point> P);

  std::vector<Point> convex_hull(std::vector<Point> P);
  std::vector<Point> construct_hull_c_all(double *data0, int ndata0);
  int construct_data(std::vector<Point> hull_c, double *&data);

  // container for contacts: x, y
  std::vector<Point> contacts_;

  int npoints_per_circle_;
  double **directions_;
};

}

#endif
