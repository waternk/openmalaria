/* This file is part of OpenMalaria.
 * 
 * Copyright (C) 2005-2009 Swiss Tropical Institute and Liverpool School Of Tropical Medicine
 * 
 * OpenMalaria is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef Hmod_UtilVectors
#define Hmod_UtilVectors

#include "global.h"
#include <gsl/gsl_vector.h>


//NOTE: maybe put everything in a Vectors namespace?

///@brief Basic operations on std::vector
/// Scale all elements of a vector by a in-situ
void vectorScale (vector<double>& vec, double a);


///@brief Comparissons on std::vector
//@{
/// Return true if, approximately, a == b
bool approxEqual (const double a, const double b);

/// Returns true if vec1 and vec2 have equal length and all elements are approximately equal.
bool approxEqual (const vector<double>& vec1, const vector<double>& vec2);
//@}


///@brief Convertions between gsl_vector and std::vector<double>
//@{
/** Convert a gsl_vector to a std::vector<double>. */
vector<double> vectorGsl2Std (const gsl_vector* vec);

/** Convert a std::vector<double> to a gsl_vector (newly allocated).
 *
 * @param vec Input vector
 * @param length Allows length to be validated. If
 *	(vec.size() != length) an exception is thrown. */
gsl_vector* vectorStd2Gsl (const vector<double>& vec, size_t length);
/// Ditto, but taking values from a double[].
gsl_vector* vectorStd2Gsl (const double* vec, size_t length);
//@}
#endif