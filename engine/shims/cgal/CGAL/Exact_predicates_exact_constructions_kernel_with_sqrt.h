// CGAL's Epeck_with_sqrt for a CGAL without GMP (the browser engine builds CGAL with CGAL_DISABLE_GMP).
//
// Orca's MeshBoolean.cpp includes libigl's igl/copyleft/cgal/mesh_boolean.h, whose assign_scalar.h
// includes this header and declares three assign_scalar() overloads on
// Exact_predicates_exact_constructions_kernel_with_sqrt::FT. CGAL defines that kernel only with CORE or
// LEDA; CORE needs GMP, so the real header stops with #error "You need LEDA or CORE installed.".
// Nothing in Orca uses the kernel; the overloads only have to compile.
//
// Without CORE/LEDA this header therefore defines the kernel over Lazy_exact_nt<Quotient<MP_Float>>: an
// exact type distinct from Epeck::FT (so the overloads do not collide with the Epeck ones) that has NO
// square root. Any code that really needed Epeck_with_sqrt (i.e. called CGAL::sqrt on it) fails to
// compile instead of silently computing something else. With CORE or LEDA the real header is used.
// This directory is searched before CGAL's own include directory for libslic3r_cgal only.
#pragma once

#include <CGAL/config.h>

#if defined(CGAL_USE_LEDA) || defined(CGAL_USE_CORE)
#include_next <CGAL/Exact_predicates_exact_constructions_kernel_with_sqrt.h>
#else

#ifndef CGAL_EXACT_PREDICATES_EXACT_CONSTRUCTIONS_KERNEL_WITH_SQRT_H
#define CGAL_EXACT_PREDICATES_EXACT_CONSTRUCTIONS_KERNEL_WITH_SQRT_H

#include <type_traits>

#include <CGAL/Exact_predicates_exact_constructions_kernel.h>
#include <CGAL/Lazy_exact_nt.h>
#include <CGAL/MP_Float.h>
#include <CGAL/Quotient.h>
#include <CGAL/Simple_cartesian.h>

namespace CGAL {

typedef Simple_cartesian<Lazy_exact_nt<Quotient<MP_Float>>> Exact_predicates_exact_constructions_kernel_with_sqrt;

static_assert(!std::is_same<Exact_predicates_exact_constructions_kernel_with_sqrt::FT, Epeck::FT>::value,
              "the placeholder sqrt kernel must not share its number type with Epeck");

} // namespace CGAL

#endif // CGAL_EXACT_PREDICATES_EXACT_CONSTRUCTIONS_KERNEL_WITH_SQRT_H
#endif // CGAL_USE_LEDA || CGAL_USE_CORE
