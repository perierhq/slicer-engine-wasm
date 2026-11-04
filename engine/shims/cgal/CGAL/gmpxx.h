// CGAL/gmpxx.h for a CGAL without GMP. Bambu Studio's bundled libigl (src/libigl, unlike the libigl
// OrcaSlicer uses) includes <CGAL/gmpxx.h> on every non-Windows build and declares assign_scalar()
// overloads on Simple_cartesian<mpq_class>::FT. Nothing in libslic3r instantiates that kernel; the
// overloads only have to compile. mpq_class is therefore an exact rational type CGAL has without GMP,
// distinct from Epeck::FT and from the placeholder sqrt kernel's FT (so no overload collides).
// Searched before CGAL's own include directory for libslic3r_cgal only (cmake/Libslic3r.cmake).
#pragma once

#include <CGAL/config.h>

#if defined(CGAL_USE_GMPXX)
#include_next <CGAL/gmpxx.h>
#else
#include <CGAL/MP_Float.h>
#include <CGAL/Quotient.h>
typedef CGAL::Quotient<CGAL::MP_Float> mpq_class;
#endif
